// src/core/CommandExecutor.cpp
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
        }
    }

    types::Result CommandExecutor::processResponse(uint16_t expectedNumber) {
        const int maxRetries = 5;
        int retries = 0;
        types::Result resultAccumulated;
        resultAccumulated.code = types::ResultCode::Skip;
        resultAccumulated.commandNumber = expectedNumber;

        // Queue for pending RESEND requests
        std::queue<uint16_t> resendQueue;

        while (retries < maxRetries) {
            auto startTime = std::chrono::steady_clock::now();
            auto timeout = std::chrono::milliseconds(5000);

            while (std::chrono::steady_clock::now() - startTime < timeout) {
                std::string response = serial_->receiveLine();

                if (response.empty() || response.find_first_not_of(" \t\r\n") == std::string::npos) {
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

                    if (token[0] == 'N') {
                        ackNumber = std::stoi(token.substr(1));
                    } else {
                        // Handle "OK 16" format (missing N)
                        try {
                            ackNumber = std::stoi(token);
                            Logger::logWarning("[CommandExecutor] Received malformed OK without 'N': OK " + token);
                        } catch (...) {
                            Logger::logError("[CommandExecutor] Cannot parse OK response: " + response);
                        }
                    }

                    if (ackNumber > 0) {
                        // Check if this completes a RESEND sequence
                        if (!resendQueue.empty() && ackNumber == resendQueue.front()) {
                            Logger::logInfo("[CommandExecutor] RESEND completed for N" + std::to_string(ackNumber));
                            resendQueue.pop();

                            // If there are more RESENDs pending, handle them
                            if (!resendQueue.empty()) {
                                uint16_t nextResend = resendQueue.front();
                                if (handleResend(nextResend)) {
                                    continue; // Process next response
                                }
                            }
                        }

                        // Check if this is our expected command
                        if (ackNumber == expectedNumber) {
                            resultAccumulated.code = types::ResultCode::Success;
                            resultAccumulated.message = "Command acknowledged";
                            return resultAccumulated;
                        } else {
                            Logger::logWarning("[CommandExecutor] ACK mismatch - Expected N" +
                                               std::to_string(expectedNumber) + " but got " +
                                               std::to_string(ackNumber));
                            // Still accept it if it's the right number
                            if (ackNumber == expectedNumber) {
                                resultAccumulated.code = types::ResultCode::Success;
                                resultAccumulated.message = "Command acknowledged (malformed)";
                                return resultAccumulated;
                            }
                        }
                    }
                }

                    // Handle RESEND
                else if (token == "RESEND") {
                    iss >> token; // FAILED or N<num>

                    if (token == "FAILED") {
                        iss >> token; // N<num>
                        if (token[0] == 'N') {
                            int failedNumber = std::stoi(token.substr(1));
                            Logger::logError("[CommandExecutor] RESEND FAILED for N" + std::to_string(failedNumber));

                            // Critical: firmware lost history, cannot recover this command
                            // Must continue from next command
                            resultAccumulated.code = types::ResultCode::Success;
                            resultAccumulated.message =
                                    "RESEND FAILED - continuing from N" + std::to_string(failedNumber);
                            return resultAccumulated;
                        }
                    } else if (token[0] == 'N') {
                        int resendNumber = std::stoi(token.substr(1));
                        Logger::logWarning("[CommandExecutor] RESEND requested for N" + std::to_string(resendNumber));

                        // Add to resend queue if not already there
                        bool alreadyQueued = false;
                        std::queue<uint16_t> tempQueue = resendQueue;
                        while (!tempQueue.empty()) {
                            if (tempQueue.front() == resendNumber) {
                                alreadyQueued = true;
                                break;
                            }
                            tempQueue.pop();
                        }

                        if (!alreadyQueued) {
                            resendQueue.push(resendNumber);
                        }

                        // Immediately handle the resend
                        if (handleResend(resendNumber)) {
                            continue; // Wait for response to resent command
                        } else {
                            Logger::logError("[CommandExecutor] Cannot RESEND N" + std::to_string(resendNumber) +
                                             " - not in history");
                            // Send dummy OK to continue
                            resultAccumulated.code = types::ResultCode::Success;
                            resultAccumulated.message = "RESEND failed - command not in history";
                            return resultAccumulated;
                        }
                    }
                }

                    // Handle DUPLICATE
                else if (token == "DUPLICATE") {
                    iss >> token; // N<num> or just number
                    int dupNumber = -1;

                    if (token[0] == 'N') {
                        dupNumber = std::stoi(token.substr(1));
                    } else {
                        // Handle malformed DUPLICATE
                        try {
                            dupNumber = std::stoi(token);
                            Logger::logWarning("[CommandExecutor] Received malformed DUPLICATE without 'N': " + token);
                        } catch (...) {
                            // No number, assume it's for current command
                            dupNumber = expectedNumber;
                        }
                    }

                    Logger::logInfo("[CommandExecutor] DUPLICATE for N" + std::to_string(dupNumber));

                    if (dupNumber == expectedNumber || dupNumber == -1) {
                        // Our command was already processed, consider it successful
                        resultAccumulated.code = types::ResultCode::Success;
                        resultAccumulated.message = "Command already processed (DUPLICATE)";
                        return resultAccumulated;
                    }
                }

                    // Handle ERROR
                else if (token == "ERR") {
                    Logger::logError("[CommandExecutor] ERROR response: " + response);
                    resultAccumulated.code = types::ResultCode::Error;
                    resultAccumulated.message = response;
                    return resultAccumulated;
                }

                    // Handle BUSY
                else if (token == "BUSY") {
                    //Logger::logInfo("[CommandExecutor] Printer BUSY, waiting...");
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    startTime = std::chrono::steady_clock::now(); // Reset timeout
                    continue;
                }
            }

            // Timeout reached
            retries++;
            Logger::logWarning("[CommandExecutor] Timeout for N" + std::to_string(expectedNumber) +
                               " (attempt " + std::to_string(retries) + "/" + std::to_string(maxRetries) + ")");

            if (retries < maxRetries) {
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
        resultAccumulated.code = types::ResultCode::Error;
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