#include "connector/processors/printer-check/PrinterCheckProcessor.hpp"
#include "logger/Logger.hpp"
#include <nlohmann/json.hpp>
#include <future>
#include <regex>
#include <sstream>

#include "application/config/ConfigManager.hpp"
#include "core/printer/job/tracking/JobTracker.hpp"
#include "core/printer/state/StateTracker.hpp"

namespace connector::processors::printer_check {
    PrinterCheckProcessor::PrinterCheckProcessor(
        std::shared_ptr<events::printer_check::PrinterCheckSender> sender,
        std::shared_ptr<core::DriverInterface> driver,
        std::shared_ptr<core::CommandExecutorQueue> commandQueue,
        const std::string &driverId)
        : sender_(sender), driver_(driver), commandQueue_(commandQueue), driverId_(driverId) {
    }

    void PrinterCheckProcessor::processPrinterCheckRequest(
        const connector::models::printer_check::PrinterCheckRequest &request) {
        auto start = std::chrono::steady_clock::now();
        Logger::logInfo("[PrinterCheckProcessor] Processing check request for job: " + request.jobId);

        try {
            connector::models::printer_check::PrinterCheckResponse response;
            response.jobId = request.jobId;
            response.driverId = driverId_;
            response.jobStatusCode = getJobStatusCode(request.jobId);
            response.printerStatusCode = getPrinterStatusCode();

            // Parallel data collection with timeout
            auto config = core::config::ConfigManager::getInstance().getPrinterCheckConfig();

            std::vector<std::future<void> > futures;
            futures.emplace_back(std::async(std::launch::async, [this, &response]() {
                collectPositionDataAsync(response);
            }));
            futures.emplace_back(std::async(std::launch::async, [this, &response]() {
                collectTemperatureDataAsync(response);
            }));
            futures.emplace_back(std::async(std::launch::async, [this, &response]() {
                collectFanDataAsync(response);
            }));
            futures.emplace_back(std::async(std::launch::async, [this, &response, &request]() {
                collectJobStatusDataAsync(response, request.jobId);
            }));
            futures.emplace_back(std::async(std::launch::async, [this, &response]() {
                collectDiagnosticDataAsync(response);
            }));

            // Wait for all with timeout
            bool allComplete = true;
            for (auto &future: futures) {
                try {
                    if (future.wait_for(std::chrono::milliseconds(config.timeoutMs)) == std::future_status::ready) {
                        future.get(); // Cattura eccezioni dal thread
                    }
                } catch (const std::exception &e) {
                    Logger::logError("[PrinterCheckProcessor] Async task failed: " + std::string(e.what()));
                    allComplete = false;
                }
            }

            if (allComplete) {
                sendResponse(response);
                auto duration = std::chrono::steady_clock::now() - start;
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
                Logger::logInfo(
                    "[PrinterCheckProcessor] Check completed in " + std::to_string(ms) + "ms for job: " + request.
                    jobId);
            } else {
                sendErrorResponse(request.jobId, "TIMEOUT_COLLECTING_DATA");
            }
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCheckProcessor] Processing error: " + std::string(e.what()));
            sendErrorResponse(request.jobId, "PROCESSING_EXCEPTION: " + std::string(e.what()));
        }
    }

    void PrinterCheckProcessor::collectPositionDataAsync(
        connector::models::printer_check::PrinterCheckResponse &response) const {
        try {
            auto &stateTracker = core::state::StateTracker::getInstance();
            auto position = driver_->motion()->getPosition();

            if (position.has_value()) {
                response.xPosition = formatDouble(position->x);
                response.yPosition = formatDouble(position->y);
                response.zPosition = formatDouble(position->z);
                response.ePosition = formatDouble(stateTracker.getCurrentEPosition());
            } else {
                Logger::logWarning("[PrinterCheckProcessor] Position query failed");
                response.xPosition = response.yPosition = response.zPosition = "QUERY_FAILED";
                response.ePosition = formatDouble(stateTracker.getCurrentEPosition()); // Use cached E
            }
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCheckProcessor] Position collection failed: " + std::string(e.what()));
            response.xPosition = response.yPosition = response.zPosition = response.ePosition = "ERROR";
        }
    }

    void PrinterCheckProcessor::collectTemperatureDataAsync(
        connector::models::printer_check::PrinterCheckResponse &response) const {
        try {
            auto &stateTracker = core::state::StateTracker::getInstance();

            // Hotend temperature
            if (stateTracker.isHotendTempFresh(3000)) {
                response.extruderTemp = formatDouble(stateTracker.getCachedHotendTemp());
                response.extruderStatus = "CACHED";
            } else {
                auto result = driver_->temperature()->getHotendTemperature();
                if (result.isSuccess() && !result.body.empty()) {
                    double temp = parseTemperatureFromResponse(result.body[0]);
                    if (temp > 0) {
                        stateTracker.updateHotendActualTemp(temp);
                        response.extruderTemp = formatDouble(temp);
                        response.extruderStatus = "LIVE";
                    } else {
                        response.extruderTemp = "PARSE_FAILED";
                        response.extruderStatus = "PARSE_ERROR";
                    }
                } else {
                    response.extruderTemp = "COMM_ERROR";
                    response.extruderStatus = "COMM_ERROR";
                }
            }

            // Bed temperature
            if (stateTracker.isBedTempFresh(3000)) {
                response.bedTemp = formatDouble(stateTracker.getCachedBedTemp());
            } else {
                auto result = driver_->temperature()->getBedTemperature();
                if (result.isSuccess() && !result.body.empty()) {
                    double temp = parseTemperatureFromResponse(result.body[0]);
                    if (temp > 0) {
                        stateTracker.updateBedActualTemp(temp);
                        response.bedTemp = formatDouble(temp);
                    } else {
                        response.bedTemp = "PARSE_FAILED";
                    }
                } else {
                    response.bedTemp = "COMM_ERROR";
                }
            }
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCheckProcessor] Temperature collection failed: " + std::string(e.what()));
            response.extruderTemp = response.bedTemp = "ERROR";
            response.extruderStatus = "ERROR";
        }
    }

    void PrinterCheckProcessor::collectFanDataAsync(
        connector::models::printer_check::PrinterCheckResponse &response) {
        try {
            auto &stateTracker = core::state::StateTracker::getInstance();
            int fanSpeed = stateTracker.getCurrentFanSpeed();

            response.fanSpeed = std::to_string(fanSpeed);
            response.fanStatus = (fanSpeed > 0) ? "RUNNING" : "STOPPED";
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCheckProcessor] Fan collection failed: " + std::string(e.what()));
            response.fanStatus = response.fanSpeed = "ERROR";
        }
    }

    void PrinterCheckProcessor::collectJobStatusDataAsync(
        connector::models::printer_check::PrinterCheckResponse &response, const std::string &jobId) {
        try {
            auto &config = core::config::ConfigManager::getInstance();
            auto &jobTracker = core::jobs::JobTracker::getInstance();
            auto &stateTracker = core::state::StateTracker::getInstance();

            auto jobInfo = jobTracker.getJobInfo(jobId);
            if (jobInfo.has_value()) {
                response.commandOffset = std::to_string(jobInfo->executedCommands);
                response.lastCommand = jobInfo->currentCommand;

                auto elapsed = jobInfo->getElapsedTime().count();
                if (elapsed > 0 && jobInfo->executedCommands > 0) {
                    double commandsPerSecond = static_cast<double>(jobInfo->executedCommands) / elapsed;
                    response.averageSpeed = formatDouble(commandsPerSecond * 60); // commands/minute
                } else {
                    response.averageSpeed = "0";
                }
            } else {
                response.commandOffset = "0";
                response.lastCommand = "NO_ACTIVE_JOB";
                response.averageSpeed = "0";
            }

            // Real-time state data
            response.feed = formatDouble(stateTracker.getCurrentFeedRate());
            response.layer = std::to_string(stateTracker.getCurrentLayer());
            response.layerHeight = formatDouble(stateTracker.getCurrentLayerHeight());

            // Apply config defaults only for zero/invalid values
            if (response.feed == "0" || response.feed == "0.000") {
                response.feed = config.getPrinterCheckConfig().defaultFeed;
            }
            if (response.layerHeight == "0" || response.layerHeight == "0.000") {
                response.layerHeight = config.getPrinterCheckConfig().defaultLayerHeight;
            }
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCheckProcessor] Job status collection failed: " + std::string(e.what()));
            response.feed = response.layer = response.layerHeight = "ERROR";
            response.commandOffset = response.averageSpeed = "ERROR";
        }
    }

    void PrinterCheckProcessor::collectDiagnosticDataAsync(
        connector::models::printer_check::PrinterCheckResponse &response) const {
        try {
            std::ostringstream exceptions;
            std::ostringstream logs;

            // Check driver state
            auto state = driver_->getState();
            if (state == core::PrintState::Error) {
                exceptions << "DRIVER_ERROR;";
            }

            // Quick endstop check with timeout
            try {
                auto endstopResult = driver_->endstop()->readEndstopStatus();
                if (endstopResult.isSuccess()) {
                    for (const auto &line: endstopResult.body) {
                        if (line.find("TRIGGERED") != std::string::npos) {
                            exceptions << "ENDSTOP_TRIGGERED;";
                        }
                        logs << "ENDSTOP:" << line << ";";
                    }
                } else {
                    exceptions << "ENDSTOP_COMM_ERROR;";
                }
            } catch (const std::exception &e) {
                exceptions << "ENDSTOP_EXCEPTION:" << e.what() << ";";
            }

            // Queue health
            if (commandQueue_) {
                auto stats = commandQueue_->getStatistics();
                logs << "QUEUE:pending=" << stats.currentQueueSize
                        << ",errors=" << stats.totalErrors
                        << ",executed=" << stats.totalExecuted << ";";

                if (stats.totalErrors > 0) {
                    double errorRate = static_cast<double>(stats.totalErrors) /
                                       std::max(stats.totalExecuted, static_cast<size_t>(1));
                    if (errorRate > 0.1) {
                        // >10% error rate
                        exceptions << "HIGH_ERROR_RATE:" << formatDouble(errorRate * 100) << "%;";
                    }
                }
            }

            response.exceptions = exceptions.str();
            response.logs = logs.str();
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCheckProcessor] Diagnostic collection failed: " + std::string(e.what()));
            response.exceptions = "DIAGNOSTIC_ERROR:" + std::string(e.what());
            response.logs = "DIAGNOSTIC_FAILED";
        }
    }

    double PrinterCheckProcessor::parseTemperatureFromResponse(const std::string &response) {
        // More robust temperature parsing
        std::vector<std::regex> patterns = {
            std::regex(R"(T:(\d+(?:\.\d+)?))"), // T:25.5
            std::regex(R"(TEMP:(\d+(?:\.\d+)?))"), // TEMP:25.5
            std::regex(R"((\d+(?:\.\d+)?)°?C?)"), // 25.5°C or 25.5
        };

        for (const auto &pattern: patterns) {
            std::smatch match;
            if (std::regex_search(response, match, pattern)) {
                try {
                    return std::stod(match[1].str());
                } catch (...) {
                    continue;
                }
            }
        }

        Logger::logWarning("[PrinterCheckProcessor] Failed to parse temperature from: " + response);
        return 0.0;
    }

    std::string PrinterCheckProcessor::parseFanFromResponse(const std::string &response) {
        // Parse fan speed from response using multiple patterns
        std::vector<std::regex> patterns = {
            std::regex(R"(S:(\d+))"), // S:255
            std::regex(R"(FAN:(\d+))"), // FAN:128
            std::regex(R"(SPEED:(\d+))"), // SPEED:50
            std::regex(R"((\d+)%)"), // 50%
        };

        for (const auto &pattern: patterns) {
            std::smatch match;
            if (std::regex_search(response, match, pattern)) {
                try {
                    int value = std::stoi(match[1].str());
                    // Normalize to 0-100 range if needed
                    if (value > 255) value = 255;
                    return std::to_string(value);
                } catch (...) {
                    continue;
                }
            }
        }

        Logger::logWarning("[PrinterCheckProcessor] Failed to parse fan speed from: " + response);
        return "UNKNOWN";
    }

    std::string PrinterCheckProcessor::formatDouble(double value) {
        // Remove trailing zeros and format consistently
        if (std::abs(value - std::round(value)) < 1e-6) {
            return std::to_string(static_cast<int>(std::round(value)));
        }

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3) << value;
        std::string result = oss.str();

        // Remove trailing zeros
        result.erase(result.find_last_not_of('0') + 1, std::string::npos);
        result.erase(result.find_last_not_of('.') + 1, std::string::npos);

        return result;
    }

    // Existing methods with minor fixes...
    std::string PrinterCheckProcessor::getJobStatusCode(const std::string &jobId) const {
        auto &tracker = core::jobs::JobTracker::getInstance();
        return tracker.getJobStateCode(jobId);
    }

    std::string PrinterCheckProcessor::getPrinterStatusCode() const {
        if (!driver_) return "UNK";

        switch (driver_->getState()) {
            case core::PrintState::Idle: return "IDL";
            case core::PrintState::Homing: return "HOM";
            case core::PrintState::Printing: return "PRI";
            case core::PrintState::Paused: return "PAU";
            case core::PrintState::Error: return "ERR";
            default: return "UNK";
        }
    }

    void PrinterCheckProcessor::sendResponse(const connector::models::printer_check::PrinterCheckResponse &response) {
        try {
            if (!response.isValid()) {
                Logger::logError("[PrinterCheckProcessor] Invalid response for job: " + response.jobId);
                return;
            }

            nlohmann::json responseJson = response.toJson();
            std::string responseMessage = responseJson.dump();

            if (sender_->sendMessage(responseMessage, driverId_)) {
                Logger::logInfo("[PrinterCheckProcessor] Response sent successfully for job: " + response.jobId);
            } else {
                Logger::logError("[PrinterCheckProcessor] Failed to send response for job: " + response.jobId);
            }
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCheckProcessor] Send failed: " + std::string(e.what()));
        }
    }

    void PrinterCheckProcessor::sendErrorResponse(const std::string &jobId, const std::string &error) {
        connector::models::printer_check::PrinterCheckResponse errorResponse;
        errorResponse.jobId = jobId;
        errorResponse.driverId = driverId_;
        errorResponse.jobStatusCode = "FAI";
        errorResponse.printerStatusCode = getPrinterStatusCode();
        errorResponse.exceptions = error;
        errorResponse.logs = "Error during check processing";

        // Set minimal error state for other fields
        errorResponse.xPosition = errorResponse.yPosition = errorResponse.zPosition = "ERROR";
        errorResponse.ePosition = errorResponse.extruderTemp = errorResponse.bedTemp = "ERROR";
        errorResponse.fanStatus = errorResponse.fanSpeed = "ERROR";
        errorResponse.feed = errorResponse.layer = errorResponse.layerHeight = "ERROR";
        errorResponse.commandOffset = errorResponse.averageSpeed = "ERROR";
        errorResponse.lastCommand = "ERROR";
        errorResponse.extruderStatus = "ERROR";

        sendResponse(errorResponse);
    }
} // namespace connector::processors::printer_check
