#include "connector/processors/printer-command/PrinterCommandProcessor.hpp"
#include "connector/events/printer-command/PrinterCommandSender.hpp"
#include "connector/models/printer-command/PrinterCommandRequest.hpp"
#include "connector/models/printer-command/PrinterCommandResponse.hpp"

#include "logger/Logger.hpp"
#include <nlohmann/json.hpp>

namespace connector::processors::printer_command {
    PrinterCommandProcessor::PrinterCommandProcessor(
        std::shared_ptr<events::printer_command::PrinterCommandSender> sender,
        std::shared_ptr<core::CommandExecutorQueue> commandQueue,
        const std::string &driverId)
        : sender_(sender), commandQueue_(commandQueue), driverId_(driverId) {
    }

    void PrinterCommandProcessor::dispatch(const connector::models::printer_command::PrinterCommandRequest &request) {
        Logger::logInfo("[PrinterCommandProcessor] Processing command request id: " + request.requestId);

        try {
            // Validate the request
            if (!request.isValid()) {
                Logger::logError("[PrinterCommandProcessor] Invalid request received");
                sendErrorResponse(request.requestId, "InvalidRequest", "Request validation failed");
                return;
            }

            // Queue command with priority for asynchronous execution
            Logger::logInfo("[PrinterCommandProcessor] Queueing command: " + request.command +
                            " with priority: " + std::to_string(request.priority));

            commandQueue_->enqueue(request.command, request.priority, request.requestId);

            // Send immediate acknowledgment that command was queued
            connector::models::printer_command::PrinterCommandResponse response(
                driverId_, request.requestId, true, "", "Command queued for execution");

            sendResponse(response);
            Logger::logInfo("[PrinterCommandProcessor] Command queued successfully: " + request.requestId);
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCommandProcessor] Unexpected error: " + std::string(e.what()));
            sendErrorResponse(request.requestId, "UnexpectedException", e.what());
        }
    }

    void PrinterCommandProcessor::sendResponse(
        const connector::models::printer_command::PrinterCommandResponse &response) {
        try {
            if (!response.isValid()) {
                Logger::logError("[PrinterCommandProcessor] Invalid response created");
                return;
            }

            nlohmann::json responseJson = response.toJson();
            std::string responseMessage = responseJson.dump();

            if (sender_->sendMessage(responseMessage, driverId_)) {
                Logger::logInfo("[PrinterCommandProcessor] Command response sent successfully");
            } else {
                Logger::logError("[PrinterCommandProcessor] Failed to send command response");
            }
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCommandProcessor] Failed to send response: " + std::string(e.what()));
        }
    }

    void PrinterCommandProcessor::sendErrorResponse(const std::string &requestId,
                                                    const std::string &exception,
                                                    const std::string &message) {
        connector::models::printer_command::PrinterCommandResponse errorResponse(
            driverId_, requestId, false, exception, message);
        sendResponse(errorResponse);
    }
}
