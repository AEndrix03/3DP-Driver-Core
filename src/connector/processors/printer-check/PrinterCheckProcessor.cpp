#include "connector/processors/printer-check/PrinterCheckProcessor.hpp"

#include <future>

#include "logger/Logger.hpp"
#include "application/config/ConfigManager.hpp"
#include "core/printer/job/tracking/JobTracker.hpp"
#include "core/printer/state/StateTracker.hpp"
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>

namespace connector::processors::printer_check {
    void PrinterCheckProcessor::processPrinterCheckRequest(
        const connector::models::printer_check::PrinterCheckRequest &request) {
        Logger::logInfo("[PrinterCheckProcessor] Processing check request for job: " + request.jobId);

        try {
            connector::models::printer_check::PrinterCheckResponse response;
            response.jobId = request.jobId;
            response.driverId = driverId_;
            response.jobStatusCode = getJobStatusCode(request.jobId);
            response.printerStatusCode = getPrinterStatusCode();

            collectPositionData(response);
            collectTemperatureData(response);
            collectFanData(response);
            collectJobStatusData(response, request.jobId);
            collectDiagnosticData(response);

            sendResponse(response);
            Logger::logInfo("[PrinterCheckProcessor] Check response sent for job: " + request.jobId);
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCheckProcessor] Processing error: " + std::string(e.what()));
            sendErrorResponse(request.jobId, e.what());
        }
    }

    void PrinterCheckProcessor::collectPositionData(
        connector::models::printer_check::PrinterCheckResponse &response) {
        try {
            auto position = driver_->motion()->getPosition();
            if (position.has_value()) {
                response.xPosition = std::to_string(position->x);
                response.yPosition = std::to_string(position->y);
                response.zPosition = std::to_string(position->z);

                // Get real E position from StateTracker
                auto &stateTracker = core::state::StateTracker::getInstance();
                response.ePosition = std::to_string(stateTracker.getCurrentEPosition());
            } else {
                // Retry with error recovery
                Logger::logWarning("[PrinterCheckProcessor] Position query failed, retrying...");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                position = driver_->motion()->getPosition();

                if (position.has_value()) {
                    response.xPosition = std::to_string(position->x);
                    response.yPosition = std::to_string(position->y);
                    response.zPosition = std::to_string(position->z);
                } else {
                    response.xPosition = "QUERY_FAILED";
                    response.yPosition = "QUERY_FAILED";
                    response.zPosition = "QUERY_FAILED";
                }
                response.ePosition = "QUERY_FAILED";
            }
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCheckProcessor] Position data collection failed: " + std::string(e.what()));
            response.xPosition = "ERROR";
            response.yPosition = "ERROR";
            response.zPosition = "ERROR";
            response.ePosition = "ERROR";
        }
    }

    void PrinterCheckProcessor::collectTemperatureData(
        connector::models::printer_check::PrinterCheckResponse &response) {
        try {
            // Real temperature queries with timeout
            auto hotendFuture = std::async(std::launch::async, [this]() {
                return driver_->temperature()->getHotendTemperature();
            });

            auto bedFuture = std::async(std::launch::async, [this]() {
                return driver_->temperature()->getBedTemperature();
            });

            // Wait with timeout
            if (hotendFuture.wait_for(std::chrono::milliseconds(2000)) == std::future_status::ready) {
                auto hotendResult = hotendFuture.get();
                if (hotendResult.isSuccess()) {
                    response.extruderTemp = parseTemperatureFromResponse(hotendResult.message);
                    response.extruderStatus = "READY";
                } else {
                    response.extruderTemp = "COMM_ERROR";
                    response.extruderStatus = "COMM_ERROR";
                }
            } else {
                response.extruderTemp = "TIMEOUT";
                response.extruderStatus = "TIMEOUT";
            }

            if (bedFuture.wait_for(std::chrono::milliseconds(2000)) == std::future_status::ready) {
                auto bedResult = bedFuture.get();
                if (bedResult.isSuccess()) {
                    response.bedTemp = parseTemperatureFromResponse(bedResult.message);
                } else {
                    response.bedTemp = "COMM_ERROR";
                }
            } else {
                response.bedTemp = "TIMEOUT";
            }
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCheckProcessor] Temperature data collection failed: " + std::string(e.what()));
            response.extruderTemp = "ERROR";
            response.extruderStatus = "ERROR";
            response.bedTemp = "ERROR";
        }
    }

    void PrinterCheckProcessor::collectFanData(
        connector::models::printer_check::PrinterCheckResponse &response) {
        try {
            // Query real fan status
            auto &stateTracker = core::state::StateTracker::getInstance();
            int currentFanSpeed = stateTracker.getCurrentFanSpeed();

            response.fanSpeed = std::to_string(currentFanSpeed);
            response.fanStatus = currentFanSpeed > 0 ? "RUNNING" : "STOPPED";

            // Validate with actual query if available
            auto statusResult = driver_->system()->printStatus();
            if (statusResult.isSuccess()) {
                for (const auto &line: statusResult.body) {
                    if (line.find("FAN") != std::string::npos) {
                        std::string fanFromStatus = parseFanFromResponse(line);
                        if (fanFromStatus != "UNKNOWN") {
                            response.fanSpeed = fanFromStatus;
                        }
                        break;
                    }
                }
            }
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCheckProcessor] Fan data collection failed: " + std::string(e.what()));
            response.fanStatus = "ERROR";
            response.fanSpeed = "ERROR";
        }
    }

    void PrinterCheckProcessor::collectJobStatusData(
        connector::models::printer_check::PrinterCheckResponse &response, const std::string &jobId) {
        auto &config = core::config::ConfigManager::getInstance();
        auto &tracker = core::jobs::JobTracker::getInstance();
        auto &stateTracker = core::state::StateTracker::getInstance();

        try {
            // Get real job info from tracker
            auto jobInfo = tracker.getJobInfo(jobId);
            if (jobInfo.has_value()) {
                response.commandOffset = std::to_string(jobInfo->executedCommands);

                // Real average speed calculation
                auto elapsedSeconds = jobInfo->getElapsedTime().count();
                if (elapsedSeconds > 0) {
                    response.averageSpeed = std::to_string(jobInfo->executedCommands / elapsedSeconds * 60);
                    // commands/minute
                } else {
                    response.averageSpeed = "0";
                }

                response.lastCommand = jobInfo->currentCommand;
            } else {
                // Fallback to queue stats
                if (commandQueue_) {
                    auto stats = commandQueue_->getStatistics();
                    response.commandOffset = std::to_string(stats.totalExecuted);
                    response.averageSpeed = "0"; // No job context available
                    response.lastCommand = "UNKNOWN_JOB";
                }
            }

            // Real feed rate from state tracker
            response.feed = std::to_string(stateTracker.getCurrentFeedRate());

            // Real layer tracking
            response.layer = std::to_string(stateTracker.getCurrentLayer());
            response.layerHeight = std::to_string(stateTracker.getCurrentLayerHeight());

            // Fallback to config defaults only if state is unavailable
            if (response.feed == "0") {
                response.feed = config.getPrinterCheckConfig().defaultFeed;
            }
            if (response.layerHeight == "0" || response.layerHeight == "0.000000") {
                response.layerHeight = config.getPrinterCheckConfig().defaultLayerHeight;
            }
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCheckProcessor] Job status data collection failed: " + std::string(e.what()));
            response.feed = "ERROR";
            response.layer = "ERROR";
            response.layerHeight = "ERROR";
            response.commandOffset = "ERROR";
            response.averageSpeed = "ERROR";
        }
    }

    void PrinterCheckProcessor::collectDiagnosticData(
        connector::models::printer_check::PrinterCheckResponse &response) {
        try {
            std::ostringstream exceptions;
            std::ostringstream logs;

            // Check printer state
            auto state = driver_->getState();
            if (state == core::PrintState::Error) {
                exceptions << "PRINTER_ERROR;";
            }

            // Real endstop check with timeout
            try {
                auto endstopFuture = std::async(std::launch::async, [this]() {
                    return driver_->endstop()->readEndstopStatus();
                });

                if (endstopFuture.wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready) {
                    auto endstopResult = endstopFuture.get();
                    if (!endstopResult.isSuccess()) {
                        exceptions << "ENDSTOP_COMM_ERROR;";
                    } else {
                        for (const auto &line: endstopResult.body) {
                            if (line.find("TRIGGERED") != std::string::npos) {
                                exceptions << "ENDSTOP_TRIGGERED;";
                            }
                            logs << "ENDSTOP:" << line << ";";
                        }
                    }
                } else {
                    exceptions << "ENDSTOP_TIMEOUT;";
                }
            } catch (const std::exception &e) {
                exceptions << "ENDSTOP_EXCEPTION:" << e.what() << ";";
            }

            // System status with real data
            try {
                auto statusFuture = std::async(std::launch::async, [this]() {
                    return driver_->system()->printStatus();
                });

                if (statusFuture.wait_for(std::chrono::milliseconds(1000)) == std::future_status::ready) {
                    auto statusResult = statusFuture.get();
                    if (statusResult.isSuccess()) {
                        for (const auto &line: statusResult.body) {
                            logs << "SYS:" << line << ";";
                        }
                    } else {
                        logs << "SYS:COMM_ERROR;";
                    }
                } else {
                    logs << "SYS:TIMEOUT;";
                }
            } catch (const std::exception &e) {
                logs << "SYS:EXCEPTION:" << e.what() << ";";
            }

            // Add queue health info
            if (commandQueue_) {
                auto stats = commandQueue_->getStatistics();
                logs << "QUEUE:size=" << stats.currentQueueSize
                        << ",errors=" << stats.totalErrors << ";";
            }

            response.exceptions = exceptions.str();
            response.logs = logs.str();
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCheckProcessor] Diagnostic data collection failed: " + std::string(e.what()));
            response.exceptions = "DIAGNOSTIC_ERROR:" + std::string(e.what());
            response.logs = "DIAGNOSTIC_COLLECTION_FAILED";
        }
    }

    std::string PrinterCheckProcessor::getJobStatusCode(const std::string &jobId) const {
        auto &tracker = core::jobs::JobTracker::getInstance();
        return tracker.getJobStateCode(jobId);
    }
} // namespace connector::processors::printer_check
