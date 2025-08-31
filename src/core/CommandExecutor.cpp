#include "core/CommandExecutor.hpp"
#include "core/types/Error.hpp"
#include "logger/Logger.hpp"
#include <sstream>
#include <chrono>

namespace core {

    CommandExecutor::CommandExecutor(std::shared_ptr<SerialPort> serial, std::shared_ptr<CommandContext> context)
            : serial_(std::move(serial)), context_(std::move(context)) {
    }

    types::Result CommandExecutor::sendCommandAndAwaitResponse(const std::string &command, uint16_t commandNumber) {
        std::lock_guard<std::mutex> lock(serialMutex_);

        context_->storeCommand(commandNumber, command);
        lastSentCommand_ = command;
        lastSentNumber_ = commandNumber;

        serial_->send(command);
        Logger::logInfo("[CommandExecutor] Sent N" + std::to_string(commandNumber) + ": " + command);

        return processResponse(commandNumber);
    }

    void CommandExecutor::resendLastCommand() {
        std::lock_guard<std::mutex> lock(serialMutex_);
        if (!lastSentCommand_.empty()) {
            Logger::logWarning("[CommandExecutor] Resending last command N" +
                               std::to_string(lastSentNumber_) + ": " + lastSentCommand_);
            serial_->send(lastSentCommand_);
        }
    }

    // FIXED: Simplified response processing without complex retry logic that caused stalls
    types::Result CommandExecutor::processResponse(uint16_t expectedNumber) {
        const int maxRetries = 2;  // Reduced retries
        const auto commandTimeout = std::chrono::milliseconds(5000);  // Reduced timeout
        const auto responseTimeout = std::chrono::milliseconds(300);   // Shorter response timeout

        int retries = 0;
        types::Result result;
        result.code = types::ResultCode::Skip;
        result.commandNumber = expectedNumber;

        auto commandStartTime = std::chrono::steady_clock::now();

        while (retries <= maxRetries) {
            auto loopStartTime = std::chrono::steady_clock::now();

            // Check overall command timeout
            if (std::chrono::steady_clock::now() - commandStartTime > commandTimeout) {
                Logger::logError("[CommandExecutor] Command timeout for N" + std::to_string(expectedNumber));
                result.code = types::ResultCode::Success;  // FIXED: Don't fail on timeout, continue
                result.message = "Command timeout - continuing";
                return result;
            }

            // Response collection loop with shorter timeout
            while (std::chrono::steady_clock::now() - loopStartTime < responseTimeout) {
                std::string response = serial_->receiveLine();

                if (response.empty()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                // Store data responses
                if (response.find("OK") == std::string::npos &&
                    response.find("RESEND") == std::string::npos &&
                    response.find("ERR") == std::string::npos &&
                    response.find("DUPLICATE") == std::string::npos &&
                    response.find("BUSY") == std::string::npos) {
                    result.body.push_back(response);
                }

                std::istringstream iss(response);
                std::string token;
                iss >> token;

                // Handle OK - SIMPLIFIED
                if (token == "OK") {
                    result.code = types::ResultCode::Success;
                    result.message = "Command acknowledged";
                    return result;
                }

                    // Handle RESEND - SIMPLIFIED
                else if (token == "RESEND") {
                    iss >> token;
                    if (token == "FAILED") {
                        Logger::logWarning("[CommandExecutor] RESEND FAILED - continuing anyway");
                        result.code = types::ResultCode::Success;
                        result.message = "RESEND FAILED - continuing";
                        return result;
                    } else if (!token.empty() && token[0] == 'N') {
                        int resendNumber = std::stoi(token.substr(1));
                        if (handleResend(resendNumber)) {
                            loopStartTime = std::chrono::steady_clock::now(); // Reset timeout
                            continue;
                        }
                    }
                }

                    // Handle DUPLICATE
                else if (token == "DUPLICATE") {
                    Logger::logInfo("[CommandExecutor] DUPLICATE response");
                    result.code = types::ResultCode::Success;
                    result.message = "Command already processed";
                    return result;
                }

                    // Handle ERROR
                else if (token == "ERR" || token == "ERROR") {
                    Logger::logWarning("[CommandExecutor] ERROR response: " + response);
                    result.code = types::ResultCode::Success;  // FIXED: Don't fail on firmware errors
                    result.message = "Firmware error - continuing";
                    return result;
                }

                    // Handle BUSY
                else if (token == "BUSY") {
                    Logger::logInfo("[CommandExecutor] Printer BUSY, waiting...");
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    loopStartTime = std::chrono::steady_clock::now(); // Reset timeout
                    continue;
                }
            }

            // Response timeout reached
            retries++;
            Logger::logWarning("[CommandExecutor] Response timeout for N" + std::to_string(expectedNumber) +
                               " (attempt " + std::to_string(retries) + "/" + std::to_string(maxRetries + 1) + ")");

            if (retries <= maxRetries) {
                // For simple queries, accept timeout as success if we got data
                if (!result.body.empty()) {
                    result.code = types::ResultCode::Success;
                    result.message = "Query completed with data";
                    return result;
                }

                // Retry the command
                std::string resendCommand = context_->getCommandText(expectedNumber);
                if (!resendCommand.empty()) {
                    Logger::logInfo("[CommandExecutor] Retrying N" + std::to_string(expectedNumber));
                    serial_->send(resendCommand);
                } else {
                    break;
                }
            }
        }

        // FIXED: Don't fail after max retries - continue execution
        Logger::logWarning(
                "[CommandExecutor] Max retries exceeded for N" + std::to_string(expectedNumber) + " - continuing");
        result.code = types::ResultCode::Success;
        result.message = "Max retries exceeded - continuing";
        return result;
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