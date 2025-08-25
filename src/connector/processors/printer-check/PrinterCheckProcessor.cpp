//
// Created by Andrea on 26/08/2025.
//

#include "connector/processors/printer-check/PrinterCheckProcessor.hpp"
#include "logger/Logger.hpp"
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>

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
        Logger::logInfo("[PrinterCheckProcessor] Processing check request for job: " + request.jobId);
        try {
            // Create response with basic identifiers
            connector::models::printer_check::PrinterCheckResponse response;
            response.jobId = request.jobId;
            response.driverId = driverId_;
            response.jobStatusCode = getJobStatusCode(request.jobId);
            response.printerStatusCode = getPrinterStatusCode();

            // Collect all printer state data
            collectPositionData(response);
            collectTemperatureData(response);
            collectFanData(response);
            collectJobStatusData(response, request.jobId);
            collectDiagnosticData(response);

            // Send the response
            sendResponse(response);
            Logger::logInfo("[PrinterCheckProcessor] Check response sent successfully for job: " + request.jobId);
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
                // E position would need additional tracking
                response.ePosition = "0.0"; // Placeholder
            } else {
                response.xPosition = "UNKNOWN";
                response.yPosition = "UNKNOWN";
                response.zPosition = "UNKNOWN";
                response.ePosition = "UNKNOWN";
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
            // Get hotend temperature
            auto hotendResult = driver_->temperature()->getHotendTemperature();
            if (hotendResult.isSuccess()) {
                response.extruderTemp = parseTemperatureFromResponse(hotendResult.message);
                response.extruderStatus = "READY";
            } else {
                response.extruderTemp = "UNKNOWN";
                response.extruderStatus = "ERROR";
            }

            // Get bed temperature
            auto bedResult = driver_->temperature()->getBedTemperature();
            if (bedResult.isSuccess()) {
                response.bedTemp = parseTemperatureFromResponse(bedResult.message);
            } else {
                response.bedTemp = "UNKNOWN";
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
            // Fan status would need to be tracked or queried
            // For now, provide basic status
            response.fanStatus = "READY";
            response.fanSpeed = "0"; // Would need actual fan speed tracking
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCheckProcessor] Fan data collection failed: " + std::string(e.what()));
            response.fanStatus = "ERROR";
            response.fanSpeed = "UNKNOWN";
        }
    }

    void PrinterCheckProcessor::collectJobStatusData(
        connector::models::printer_check::PrinterCheckResponse &response, const std::string &jobId) {
        try {
            // Get command queue statistics for job tracking
            if (commandQueue_) {
                auto stats = commandQueue_->getStatistics();
                response.commandOffset = std::to_string(stats.totalExecuted);
                response.averageSpeed = stats.totalExecuted > 0 ? std::to_string(stats.totalExecuted / 60) : "0";
                // Very basic calculation
                // Queue size gives us an indication of job progress
                if (stats.currentQueueSize > 0) {
                    Logger::logInfo(
                        "[PrinterCheckProcessor] Queue has " + std::to_string(stats.currentQueueSize) +
                        " commands pending");
                }
            }
            // Default values that would need proper job tracking implementation
            response.feed = "1000"; // Default feedrate
            response.layer = "0"; // Would need layer tracking
            response.layerHeight = "0.2"; // Default layer height
            response.lastCommand = ""; // Would need last command tracking
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
            // Check for any exceptions or errors
            std::ostringstream exceptions;
            std::ostringstream logs;

            // Check printer state
            auto state = driver_->getState();
            if (state == core::PrintState::Error) {
                exceptions << "PRINTER_ERROR;";
            }

            // Check endstops
            auto endstopResult = driver_->endstop()->readEndstopStatus();
            if (!endstopResult.isSuccess()) {
                exceptions << "ENDSTOP_ERROR;";
            } else {
                for (const auto &line: endstopResult.body) {
                    if (line.find("TRIGGERED") != std::string::npos) {
                        exceptions << "ENDSTOP_TRIGGERED;";
                    }
                    logs << line << ";";
                }
            }

            // Get system status
            auto statusResult = driver_->system()->printStatus();
            if (statusResult.isSuccess()) {
                for (const auto &line: statusResult.body) {
                    logs << line << ";";
                }
            }

            response.exceptions = exceptions.str();
            response.logs = logs.str();
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCheckProcessor] Diagnostic data collection failed: " + std::string(e.what()));
            response.exceptions = "DIAGNOSTIC_ERROR";
            response.logs = "DIAGNOSTIC_COLLECTION_FAILED";
        }
    }

    std::string PrinterCheckProcessor::getJobStatusCode(const std::string &jobId) const {
        // TODO: This should integrate with PrintJobManager when available
        // For now, we use basic logic based on printer state and queue status
        if (!commandQueue_) {
            return "UNK";
        }
        auto stats = commandQueue_->getStatistics();
        auto printerState = driver_->getState();
        // Basic job state inference based on printer state and queue
        if (printerState == core::PrintState::Error) {
            return "FAI"; // Failed
        } else if (printerState == core::PrintState::Paused) {
            return "PAU"; // Paused
        } else if (printerState == core::PrintState::Printing && stats.currentQueueSize > 0) {
            return "RUN"; // Running
        } else if (printerState == core::PrintState::Printing && stats.currentQueueSize == 0) {
            return "CMP"; // Completed (was printing, now no more commands)
        } else {
            // No active job or job status unknown
            return "UNK";
        }
    }

    std::string PrinterCheckProcessor::getPrinterStatusCode() const {
        if (!driver_) {
            return "UNK";
        }

        switch (driver_->getState()) {
            case core::PrintState::Idle:
                return "IDL";
            case core::PrintState::Homing:
                return "HOM";
            case core::PrintState::Printing:
                return "PRI";
            case core::PrintState::Paused:
                return "PAU";
            case core::PrintState::Error:
                return "ERR";
            default:
                return "UNK";
        }
    }

    std::string PrinterCheckProcessor::parseTemperatureFromResponse(const std::string &response) const {
        // Parse temperature from response using regex
        std::regex tempRegex(R"(T:(\d+(?:\.\d+)?))");
        std::smatch match;
        if (std::regex_search(response, match, tempRegex)) {
            return match[1].str();
        }
        // Fallback - look for any number
        std::regex numberRegex(R"((\d+(?:\.\d+)?))");
        if (std::regex_search(response, match, numberRegex)) {
            return match[1].str();
        }
        return "UNKNOWN";
    }

    std::string PrinterCheckProcessor::parseFanFromResponse(const std::string &response) const {
        // Parse fan speed from response using regex
        std::regex fanRegex(R"(S:(\d+))");
        std::smatch match;
        if (std::regex_search(response, match, fanRegex)) {
            return match[1].str();
        }
        return "UNKNOWN";
    }

    void PrinterCheckProcessor::sendResponse(
        const connector::models::printer_check::PrinterCheckResponse &response) {
        try {
            if (!response.isValid()) {
                Logger::logError("[PrinterCheckProcessor] Invalid response created");
                return;
            }

            nlohmann::json responseJson = response.toJson();
            std::string responseMessage = responseJson.dump();

            if (sender_->sendMessage(responseMessage, driverId_)) {
                Logger::logInfo("[PrinterCheckProcessor] Check response sent successfully");
            } else {
                Logger::logError("[PrinterCheckProcessor] Failed to send check response");
            }
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCheckProcessor] Failed to send response: " + std::string(e.what()));
        }
    }

    void PrinterCheckProcessor::sendErrorResponse(const std::string &jobId, const std::string &error) {
        connector::models::printer_check::PrinterCheckResponse errorResponse;
        errorResponse.jobId = jobId;
        errorResponse.driverId = driverId_;
        errorResponse.jobStatusCode = "FAI"; // Failed
        errorResponse.printerStatusCode = getPrinterStatusCode();
        errorResponse.exceptions = error;
        errorResponse.logs = "Error during printer check processing";

        // Set other fields to error state
        errorResponse.xPosition = "ERROR";
        errorResponse.yPosition = "ERROR";
        errorResponse.zPosition = "ERROR";
        errorResponse.ePosition = "ERROR";
        errorResponse.extruderTemp = "ERROR";
        errorResponse.bedTemp = "ERROR";
        errorResponse.fanStatus = "ERROR";
        errorResponse.fanSpeed = "ERROR";

        sendResponse(errorResponse);
    }
} // namespace connector::processors::printer_check
