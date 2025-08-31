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

            // Ensure command queue is running
            if (!commandQueue_->isRunning()) {
                Logger::logInfo("[PrinterCommandProcessor] Starting command executor queue");
                commandQueue_->start();

                // Wait a moment for queue to initialize
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            // Split command by ';' separator
            std::vector<std::string> commands = splitCommands(request.command);
            Logger::logInfo("[PrinterCommandProcessor] Queueing " + std::to_string(commands.size()) +
                            " command(s) with priority: " + std::to_string(request.priority));

            // IMPORTANT: Use empty string for jobId - these are individual commands, not jobs!
            // Only PrintJobManager should create jobs
            std::string jobId = ""; // NO JOB for individual commands

            // Log the actual commands being queued
            for (size_t i = 0; i < commands.size() && i < 5; ++i) {
                Logger::logInfo("[PrinterCommandProcessor]   Command[" + std::to_string(i) + "]: " + commands[i]);
            }
            if (commands.size() > 5) {
                Logger::logInfo("[PrinterCommandProcessor]   ... and " + std::to_string(commands.size() - 5) + " more");
            }

            // Enqueue all commands with priority but NO jobId
            commandQueue_->enqueueCommands(commands, request.priority, jobId);

            // Force wake up the queue multiple times
            for (int i = 0; i < 5; ++i) {
                commandQueue_->wakeUp();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            // Send immediate acknowledgment
            connector::models::printer_command::PrinterCommandResponse response(
                    driverId_,
                    request.requestId,
                    true,
                    "",
                    "Commands queued for execution (" + std::to_string(commands.size()) + " commands)"
            );

            sendResponse(response);
            Logger::logInfo("[PrinterCommandProcessor] Commands queued successfully for request: " + request.requestId);

            // Double-check queue is processing
            if (commandQueue_->getQueueSize() > 0 && commandQueue_->isRunning()) {
                Logger::logInfo("[PrinterCommandProcessor] Queue confirmed active with " +
                                std::to_string(commandQueue_->getQueueSize()) + " pending commands");
            }

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
                Logger::logInfo("[PrinterCommandProcessor] Response sent for request: " +
                                responseJson["requestId"].get<std::string>());
            } else {
                Logger::logError("[PrinterCommandProcessor] Failed to send response");
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

    std::vector<std::string> PrinterCommandProcessor::splitCommands(const std::string &command) {
        std::vector<std::string> commands;
        size_t start = 0;
        size_t pos = 0;

        // Split by semicolon
        while ((pos = command.find(';', start)) != std::string::npos) {
            std::string segment = command.substr(start, pos - start);
            // Trim whitespace
            size_t first = segment.find_first_not_of(" \t\r\n");
            if (first != std::string::npos) {
                size_t last = segment.find_last_not_of(" \t\r\n");
                segment = segment.substr(first, last - first + 1);
                if (!segment.empty()) {
                    commands.push_back(segment);
                }
            }
            start = pos + 1;
        }

        // Add last segment
        if (start < command.length()) {
            std::string segment = command.substr(start);
            size_t first = segment.find_first_not_of(" \t\r\n");
            if (first != std::string::npos) {
                size_t last = segment.find_last_not_of(" \t\r\n");
                segment = segment.substr(first, last - first + 1);
                if (!segment.empty()) {
                    commands.push_back(segment);
                }
            }
        }

        // If no commands found, use original
        if (commands.empty() && !command.empty()) {
            commands.push_back(command);
        }

        return commands;
    }
}