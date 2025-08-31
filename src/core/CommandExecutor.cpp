#include "core/CommandExecutor.hpp"
#include "core/types/Error.hpp"
#include "logger/Logger.hpp"
#include <sstream>
#include <chrono>
#include <queue>

namespace core {

    CommandExecutor::CommandExecutor(std::shared_ptr<SerialPort> serial, std::shared_ptr<CommandContext> context)
            : serial_(std::move(serial)), context_(std::move(context)) {
    }

    types::Result CommandExecutor::sendCommandAndAwaitResponse(const std::string &command, uint16_t commandNumber) {
        std::lock_guard<std::mutex> lock(serialMutex_);

        // Store command BEFORE sending for potential RESEND
        context_->storeCommand(commandNumber, command);

        // Store as last sent command for health recovery
        lastSentCommand_ = command;
        lastSentNumber_ = commandNumber;

        // Send the command
        serial_->send(command);
        Logger::logInfo("[CommandExecutor] Sent N" + std::to_string(commandNumber) + ": " + command);

        // Process response with proper RESEND handling
        return processResponse(commandNumber);
    }

    void CommandExecutor::resendLastCommand() {
        std::lock_guard<std::mutex> lock(serialMutex_);
        if (!lastSentCommand_.empty()) {
            Logger::logWarning("[CommandExecutor] Health recovery: resending N" +
                               std::to_string(lastSentNumber_) + ": " + lastSentCommand_);
            serial_->send(lastSentCommand_);

            // Clear any stuck state
            lastSentCommand_.clear();
            lastSentNumber_ = 0;
        }
    }

    types::Result CommandExecutor::processResponse(uint16_t expectedNumber) {
        const int maxRetries = 3;  // Reduced from 5
        const auto commandTimeout = std::chrono::milliseconds(10000);  // 10 seconds per command
        const auto responseTimeout = std::chrono::milliseconds(500);   // 500ms between responses

        int retries = 0;
        types::Result resultAccumulated;
        resultAccumulated.code = types::ResultCode::Skip;
        resultAccumulated.commandNumber = expectedNumber;

        // Queue for pending RESEND requests
        std::queue<uint16_t> resendQueue;

        auto commandStartTime = std::chrono::steady_clock::now();

        while (retries < maxRetries) {
            auto loopStartTime = std::chrono::steady_clock::now();

            // Check overall command timeout
            if (std::chrono::steady_clock::now() - commandStartTime > commandTimeout) {
                Logger::logError("[CommandExecutor] Command timeout for N" + std::to_string(expectedNumber));
                resultAccumulated.code = types::ResultCode::Timeout;
                resultAccumulated.message = "Command timeout after 10 seconds";
                return resultAccumulated;
            }

            // Inner loop with shorter timeout for individual responses
            while (std::chrono::steady_clock::now() - loopStartTime < responseTimeout) {
                std::string response = serial_->receiveLine();

                if (response.empty() || response.find_first_not_of(" \t\r\n") == std::string::npos) {
                    // Brief sleep to prevent busy waiting
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                // Store non-control responses
                if (response.find("OK") == std::string::npos &&
                    response.find("RESEND") == std::string::npos &&
                    response.find("ERR") == std::string::npos &&
                    response.find("DUPLICATE") == std::string::npos &&
                    response.find("BUSY") == std::string::npos) {
                    resultAccumulated.body.push_back(response);
                }

                // Parse response
                std::istringstream iss(response);
                std::string token;
                iss >> token;

                // Handle OK
                if (token == "OK") {
                    iss >> token; // N<num> or just number
                    int ackNumber = -1;

                    if (!token.empty() && token[0] == 'N') {
                        try {
                            ackNumber = std::stoi(token.substr(1));
                        } catch (...) {
                            Logger::logWarning("[CommandExecutor] Cannot parse OK N number: " + token);
                        }
                    } else if (!token.empty()) {
                        // Handle "OK 16" format (missing N)
                        try {
                            ackNumber = std::stoi(token);
                            Logger::logWarning("[CommandExecutor] Received malformed OK without 'N': OK " + token);
                        } catch (...) {
                            Logger::logError("[CommandExecutor] Cannot parse OK response: " + response);
                        }
                    }

                    // Accept OK even without number for simple commands
                    if (ackNumber == expectedNumber || ackNumber == -1) {
                        resultAccumulated.code = types::ResultCode::Success;
                        resultAccumulated.message = "Command acknowledged";
                        return resultAccumulated;
                    }

                    // Check if this completes a RESEND sequence
                    if (ackNumber > 0 && !resendQueue.empty() && ackNumber == resendQueue.front()) {
                        Logger::logInfo("[CommandExecutor] RESEND completed for N" + std::to_string(ackNumber));
                        resendQueue.pop();
                    }
                }

                    // Handle RESEND
                else if (token == "RESEND") {
                    iss >> token; // FAILED or N<num>

                    if (token == "FAILED") {
                        iss >> token; // N<num>
                        if (!token.empty() && token[0] == 'N') {
                            int failedNumber = std::stoi(token.substr(1));
                            Logger::logError("[CommandExecutor] RESEND FAILED for N" + std::to_string(failedNumber));

                            // Critical: firmware lost history, cannot recover this command
                            resultAccumulated.code = types::ResultCode::Success;
                            resultAccumulated.message = "RESEND FAILED - continuing";
                            return resultAccumulated;
                        }
                    } else if (!token.empty() && token[0] == 'N') {
                        int resendNumber = std::stoi(token.substr(1));
                        Logger::logWarning("[CommandExecutor] RESEND requested for N" + std::to_string(resendNumber));

                        // Immediately handle the resend
                        if (handleResend(resendNumber)) {
                            loopStartTime = std::chrono::steady_clock::now(); // Reset timeout
                            continue;
                        } else {
                            Logger::logError("[CommandExecutor] Cannot RESEND N" + std::to_string(resendNumber));
                            // Continue anyway
                            resultAccumulated.code = types::ResultCode::Success;
                            resultAccumulated.message = "RESEND failed - command not in history";
                            return resultAccumulated;
                        }
                    }
                }

                    // Handle DUPLICATE
                else if (token == "DUPLICATE") {
                    Logger::logInfo("[CommandExecutor] DUPLICATE response received");
                    resultAccumulated.code = types::ResultCode::Success;
                    resultAccumulated.message = "Command already processed (DUPLICATE)";
                    return resultAccumulated;
                }

                    // Handle ERROR
                else if (token == "ERR" || token == "ERROR") {
                    Logger::logError("[CommandExecutor] ERROR response: " + response);
                    resultAccumulated.code = types::ResultCode::Error;
                    resultAccumulated.message = response;
                    return resultAccumulated;
                }

                    // Handle BUSY
                else if (token == "BUSY") {
                    Logger::logInfo("[CommandExecutor] Printer BUSY, waiting...");
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    loopStartTime = std::chrono::steady_clock::now(); // Reset timeout
                    continue;
                }
            }

            // Response timeout reached for this retry
            retries++;
            Logger::logWarning("[CommandExecutor] Response timeout for N" + std::to_string(expectedNumber) +
                               " (attempt " + std::to_string(retries) + "/" + std::to_string(maxRetries) + ")");

            if (retries < maxRetries) {
                // For simple queries, accept timeout as success
                if (expectedNumber > 0 &&
                    context_->getCommandText(expectedNumber).find("M114") != std::string::npos) {
                    // Position query - if we got body data, consider it success
                    if (!resultAccumulated.body.empty()) {
                        resultAccumulated.code = types::ResultCode::Success;
                        resultAccumulated.message = "Query completed with partial response";
                        return resultAccumulated;
                    }
                }

                // Resend the current command
                std::string resendCommand = context_->getCommandText(expectedNumber);
                if (!resendCommand.empty()) {
                    Logger::logInfo("[CommandExecutor] Retrying N" + std::to_string(expectedNumber));
                    serial_->send(resendCommand);
                } else {
                    Logger::logError("[CommandExecutor] Cannot retry - command not in history");
                    break;
                }
            }
        }

        Logger::logError("[CommandExecutor] Max retries exceeded for N" + std::to_string(expectedNumber));
        resultAccumulated.code = types::ResultCode::Timeout;
        resultAccumulated.message = "Max retries exceeded";
        return resultAccumulated;
    }

    bool CommandExecutor::handleResend(uint16_t commandNumber) {
        std::string resendCommand = context_->getCommandText(commandNumber);

        if (resendCommand.empty()) {
            Logger::logError("[CommandExecutor] Cannot RESEND N" + std::to_string(commandNumber) + " - not in history");
            return false;
        }

        Logger::logInfo("[CommandExecutor] Resending N" + std::to_string(commandNumber) + ": " + resendCommand);
        serial_->send(resendCommand);
        return true;
    }

} // namespace core