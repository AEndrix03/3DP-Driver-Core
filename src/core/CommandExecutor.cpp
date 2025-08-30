// src/core/CommandExecutor.cpp
#include "core/CommandExecutor.hpp"
#include "core/types/Error.hpp"
#include "logger/Logger.hpp"
#include <sstream>
#include <chrono>

namespace core {

    CommandExecutor::CommandExecutor(std::shared_ptr<SerialPort> serial, std::shared_ptr<CommandContext> context)
            : serial_(std::move(serial)), context_(std::move(context)) {
    }

    types::Result CommandExecutor::sendCommandAndAwaitResponse(const std::string &command, uint16_t expectedNumber) {
        std::lock_guard<std::mutex> lock(serialMutex_);

        // Store command BEFORE sending for potential RESEND
        context_->storeCommand(expectedNumber, command);

        serial_->send(command);

        int retries = 0;
        const int maxRetries = 3;
        const int baseTimeoutMs = 2000;

        types::Result resultAccumulated;
        resultAccumulated.code = types::ResultCode::Skip;
        resultAccumulated.commandNumber = expectedNumber;

        while (retries <= maxRetries) {
            auto startTime = std::chrono::steady_clock::now();
            auto maxTimeout = std::chrono::milliseconds(baseTimeoutMs + retries * 1000);

            while (true) {
                if (std::chrono::steady_clock::now() - startTime > maxTimeout) {
                    Logger::logWarning("[CommandExecutor] Timeout N" + std::to_string(expectedNumber) +
                                       " (attempt " + std::to_string(retries + 1) + ")");
                    break;
                }

                std::string response = serial_->receiveLine();

                if (response == "CONN_LOST" || response == "SERIAL_ERROR") {
                    Logger::logError("[CommandExecutor] Serial connection issue for N" +
                                     std::to_string(expectedNumber));
                    return {types::ResultCode::Error, "Serial connection lost"};
                }

                if (response.empty() || response.find_first_not_of(" \t\r\n") == std::string::npos) {
                    continue;
                }

                // Store non-OK responses for debug
                if (response.find("OK") == std::string::npos &&
                    response.find("BUSY") == std::string::npos &&
                    response.find("RESEND") == std::string::npos) {
                    resultAccumulated.body.push_back(response);
                }

                types::Result result = parseResponse(response, expectedNumber);

                if (result.isSuccess()) {
                    resultAccumulated.code = types::ResultCode::Success;
                    resultAccumulated.message = result.message;
                    return resultAccumulated;
                }

                if (result.isSkip()) {
                    continue;
                }

                if (result.isBusy()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50 * (retries + 1)));
                    startTime = std::chrono::steady_clock::now();
                    continue;
                }

                if (result.isError()) {
                    // Check if it's a RESEND FAILED error
                    if (result.message.find("RESEND FAILED") != std::string::npos) {
                        Logger::logError("[CommandExecutor] RESEND FAILED - firmware lost command history");
                        // Continue execution instead of blocking
                        return {types::ResultCode::Success, "RESEND FAILED - continuing"};
                    }
                    Logger::logWarning("[CommandExecutor] Error response: " + result.message);
                    break;
                }
            }

            retries++;
            if (retries <= maxRetries) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100 * retries));
                Logger::logWarning("[CommandExecutor] Retry " + std::to_string(retries) +
                                   "/" + std::to_string(maxRetries) + " for N" + std::to_string(expectedNumber));
                serial_->send(command);
            }
        }

        Logger::logError("[CommandExecutor] Max retries exceeded for N" + std::to_string(expectedNumber));
        return {types::ResultCode::Error, "Max retries exceeded"};
    }

    types::Result CommandExecutor::parseResponse(const std::string &response, uint16_t expectedNumber) {
        std::istringstream iss(response);
        std::string token;
        iss >> token;

        if (token == "OK" || token == "DUPLICATE") {
            iss >> token; // N<num>
            if (token[0] == 'N') {
                int receivedNumber = std::stoi(token.substr(1));
                if (receivedNumber != expectedNumber) {
                    Logger::logWarning("[CommandExecutor] ACK mismatch - Expected N" +
                                       std::to_string(expectedNumber) + " but got N" +
                                       std::to_string(receivedNumber));
                    // Don't fail, just warn
                }
            }
            return {types::ResultCode::Success, "Command acknowledged."};
        }

        if (token == "BUSY") {
            return {types::ResultCode::Busy, "Busy Serial: Command is processing."};
        }

        if (token == "RESEND") {
            iss >> token; // N<num> or FAILED

            if (token == "FAILED") {
                // Handle "RESEND FAILED N###"
                iss >> token; // N<num>
                if (token[0] == 'N') {
                    int failedNumber = std::stoi(token.substr(1));
                    Logger::logError("[CommandExecutor] RESEND FAILED for N" + std::to_string(failedNumber));
                    return {types::ResultCode::Error, "RESEND FAILED N" + std::to_string(failedNumber)};
                }
            } else if (token[0] == 'N') {
                int requestedNumber = std::stoi(token.substr(1));
                std::string resendCommand = context_->getCommandText(requestedNumber);

                if (resendCommand.empty()) {
                    Logger::logError("[CommandExecutor] Cannot RESEND N" + std::to_string(requestedNumber) +
                                     " - not in history");
                    // Send a dummy OK to continue
                    return {types::ResultCode::Success, "RESEND failed but continuing"};
                }

                Logger::logWarning("[RESEND] Resending command N" + std::to_string(requestedNumber));
                serial_->send(resendCommand);

                // Wait for response to the resent command
                return sendCommandAndAwaitResponse(resendCommand, requestedNumber);
            }
        }

        if (token == "ERR") {
            return {types::ResultCode::Error, response};
        }

        return {types::ResultCode::Skip, "Response message. Skipping results..."};
    }

} // namespace core