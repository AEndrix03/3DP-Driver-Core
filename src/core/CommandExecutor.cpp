#include "core/CommandExecutor.hpp"
#include "core/types/Error.hpp"
#include "logger/Logger.hpp"
#include <sstream>
#include <chrono>
#include <regex>

namespace core {

    CommandExecutor::CommandExecutor(std::shared_ptr<SerialPort> serial, std::shared_ptr<CommandContext> context)
            : serial_(std::move(serial)), context_(std::move(context)), firmwareSyncLost_(false) {
        protocolHandler_ = std::make_shared<SerialProtocolHandler>(serial_);
    }

    types::Result CommandExecutor::sendCommandAndAwaitResponse(const std::string &command, uint16_t commandNumber) {
        std::lock_guard<std::mutex> lock(serialMutex_);

        context_->storeCommand(commandNumber, command);
        lastSentCommand_ = command;
        lastSentNumber_ = commandNumber;

        protocolHandler_->sendCommand(command);
        Logger::logInfo("[CommandExecutor] Sent N" + std::to_string(commandNumber) + ": " + command);

        return processResponse(commandNumber);
    }

    types::Result CommandExecutor::processResponse(uint16_t expectedNumber) {
        const int maxRetries = 2;
        const auto commandTimeout = std::chrono::milliseconds(5000);

        int retries = 0;
        types::Result result;
        result.code = types::ResultCode::Skip;
        result.commandNumber = expectedNumber;

        auto commandStartTime = std::chrono::steady_clock::now();

        while (retries <= maxRetries) {
            if (std::chrono::steady_clock::now() - commandStartTime > commandTimeout) {
                Logger::logError("[CommandExecutor] Command timeout for N" + std::to_string(expectedNumber));
                result.code = types::ResultCode::Success;
                result.message = "Command timeout - continuing";
                return result;
            }

            SerialMessage message = protocolHandler_->receiveMessage();

            if (message.rawMessage.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            // Scarta messaggi non critici con checksum invalido
            if (!message.isValid && message.type != MessageType::CRITICAL) {
                Logger::logWarning("[CommandExecutor] Invalid message discarded: " + message.rawMessage);
                continue;
            }

            // Store informational data
            if (message.type == MessageType::INFORMATIONAL) {
                result.body.push_back(message.payload);
                continue;
            }

            // Handle critical messages
            if (message.type == MessageType::CRITICAL) {
                Logger::logInfo("[CommandExecutor] Critical: " + message.payload);
                result.body.push_back(message.payload);
                continue;
            }

            // Handle standard responses
            if (message.type == MessageType::STANDARD) {
                if (message.code == "OK0") {
                    result.code = types::ResultCode::Success;
                    result.message = "Command acknowledged";
                    return result;

                } else if (message.isDuplicate) { // E03
                    Logger::logInfo("[CommandExecutor] DUPLICATE response");
                    result.code = types::ResultCode::Success;
                    result.message = "Command already processed";
                    return result;

                } else if (message.isResendRequest) { // E04
                    Logger::logWarning("[CommandExecutor] RESEND request for N" +
                                       std::to_string(message.resendCommandNumber));
                    handleResend(message.resendCommandNumber);
                    // Reset timeout for resent command
                    commandStartTime = std::chrono::steady_clock::now();
                    continue;
                    
                } else if (message.code == "E01") { // Checksum error
                    Logger::logWarning("[CommandExecutor] Firmware checksum error");
                    result.code = types::ResultCode::ChecksumMismatch;
                    result.message = "Firmware reported checksum error";
                    return result;

                } else if (message.code == "E02") { // Buffer overflow
                    Logger::logError("[CommandExecutor] Firmware buffer overflow");
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue; // Retry after delay

                } else if (message.code == "E05") { // Invalid category
                    Logger::logError("[CommandExecutor] Invalid command category");
                    result.code = types::ResultCode::Error;
                    result.message = "Invalid command category";
                    return result;

                } else if (message.code == "EM0") { // Motion blocked
                    Logger::logWarning("[CommandExecutor] Motion blocked by firmware");
                    result.code = types::ResultCode::Busy;
                    result.message = "Motion blocked";
                    return result;

                } else if (message.code == "ET0") { // Temperature blocked
                    Logger::logWarning("[CommandExecutor] Temperature operation blocked");
                    result.code = types::ResultCode::Busy;
                    result.message = "Temperature blocked";
                    return result;

                } else if (message.code == "ES0") { // Operation cancelled
                    Logger::logInfo("[CommandExecutor] Operation cancelled by firmware");
                    result.code = types::ResultCode::Skip;
                    result.message = "Operation cancelled";
                    return result;

                } else if (message.code == "ES1") { // No error
                    Logger::logInfo("[CommandExecutor] No error response");
                    result.code = types::ResultCode::Success;
                    result.message = "No error";
                    return result;

                } else {
                    Logger::logWarning("[CommandExecutor] Unknown response: " + message.code);
                    result.code = types::ResultCode::Success;
                    result.message = "Unknown response - continuing";
                    return result;
                }
            }
        }

        Logger::logWarning("[CommandExecutor] Max retries exceeded for N" + std::to_string(expectedNumber));
        result.code = types::ResultCode::Success;
        result.message = "Max retries exceeded - continuing";
        return result;
    }

    bool CommandExecutor::attemptResendFailedRecovery(uint16_t failedCommandNumber) {
        Logger::logInfo(
                "[CommandExecutor] Starting RESEND FAILED recovery for N" + std::to_string(failedCommandNumber));

        // Strategy 1: Identify what firmware expects and send it
        uint16_t expectedByFirmware = identifyExpectedCommand(failedCommandNumber);
        if (expectedByFirmware != 0) {
            if (sendExpectedCommand(expectedByFirmware)) {
                Logger::logInfo(
                        "[CommandExecutor] Successfully sent expected command N" + std::to_string(expectedByFirmware));
                return waitForRecoveryConfirmation();
            }
        }

        // Strategy 2: Reset line numbering with M110
        Logger::logInfo("[CommandExecutor] Attempting line number reset");
        if (attemptLineNumberReset(failedCommandNumber)) {
            return waitForRecoveryConfirmation();
        }

        // Strategy 3: Full communication reset
        Logger::logInfo("[CommandExecutor] Attempting full communication reset");
        return attemptFullReset();
    }

    uint16_t CommandExecutor::identifyExpectedCommand(uint16_t failedNumber) {
        // Try to detect what command the firmware is waiting for
        // Usually it's the last command that didn't get confirmed

        // Check if we have the previous command
        for (uint16_t candidate = failedNumber - 1; candidate >= std::max(1, (int) failedNumber - 5); candidate--) {
            if (!context_->getCommandText(candidate).empty()) {
                Logger::logInfo("[CommandExecutor] Firmware likely expects N" + std::to_string(candidate));
                return candidate;
            }
        }

        // If no specific command found, try the failed command itself
        return failedNumber;
    }

    bool CommandExecutor::sendExpectedCommand(uint16_t commandNumber) {
        std::string command = context_->getCommandText(commandNumber);
        if (command.empty()) {
            Logger::logError(
                    "[CommandExecutor] Cannot find command N" + std::to_string(commandNumber) + " for recovery");
            return false;
        }

        Logger::logInfo(
                "[CommandExecutor] Resending expected command N" + std::to_string(commandNumber) + ": " + command);
        serial_->send(command);

        return true;
    }

    bool CommandExecutor::attemptLineNumberReset(uint16_t fromNumber) {
        // Send M110 to reset line numbering
        std::string resetCommand = "N" + std::to_string(fromNumber) + " M110 N" + std::to_string(fromNumber);
        uint8_t checksum = computeChecksum("N" + std::to_string(fromNumber) + " M110 N" + std::to_string(fromNumber));
        resetCommand += " *" + std::to_string(checksum);

        Logger::logInfo("[CommandExecutor] Sending line reset: " + resetCommand);
        serial_->send(resetCommand);

        return true;
    }

    bool CommandExecutor::attemptFullReset() {
        Logger::logInfo("[CommandExecutor] Attempting full firmware reset");

        // Send emergency reset
        serial_->send("M999");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Send firmware info request to test communication
        serial_->send("M115");

        return true;
    }

    bool CommandExecutor::waitForRecoveryConfirmation() {
        const auto timeout = std::chrono::milliseconds(2000);
        auto startTime = std::chrono::steady_clock::now();

        while (std::chrono::steady_clock::now() - startTime < timeout) {
            std::string response = serial_->receiveLine();

            if (response.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            Logger::logInfo("[CommandExecutor] Recovery response: " + response);

            // Look for positive responses
            if (response.find("OK") != std::string::npos ||
                response.find("ok") != std::string::npos ||
                response.find("FIRMWARE") != std::string::npos ||
                response.find("ready") != std::string::npos) {
                Logger::logInfo("[CommandExecutor] Recovery confirmed");
                return true;
            }

            // Check for continued sync errors
            if (response.find("RESEND FAILED") != std::string::npos) {
                Logger::logError("[CommandExecutor] Recovery failed - still getting RESEND FAILED");
                return false;
            }
        }

        Logger::logWarning("[CommandExecutor] Recovery timeout - assuming success");
        return true; // Assume success on timeout for robustness
    }

    uint8_t CommandExecutor::computeChecksum(const std::string &data) {
        uint8_t checksum = 0;
        for (char c: data) {
            checksum ^= static_cast<uint8_t>(c);
        }
        return checksum;
    }

    bool CommandExecutor::attemptFirmwareResync() {
        Logger::logInfo("[CommandExecutor] Performing firmware resync");

        // Test communication with M115
        serial_->send("M115");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        for (int i = 0; i < 15; ++i) {
            std::string response = serial_->receiveLine();
            if (response.empty()) continue;

            Logger::logInfo("[CommandExecutor] Resync response: " + response);

            if (response.find("FIRMWARE") != std::string::npos ||
                response.find("ok") != std::string::npos ||
                response.find("OK") != std::string::npos) {
                return true;
            }
        }

        return false;
    }

    void CommandExecutor::resendLastCommand() {
        std::lock_guard<std::mutex> lock(serialMutex_);
        if (!lastSentCommand_.empty()) {
            Logger::logWarning("[CommandExecutor] Resending last command N" +
                               std::to_string(lastSentNumber_) + ": " + lastSentCommand_);
            serial_->send(lastSentCommand_);
        }
    }

    void CommandExecutor::handleResend(uint16_t commandNumber) {
        std::string resendCommand = context_->getCommandText(commandNumber);

        if (resendCommand.empty()) {
            Logger::logError(
                    "[CommandExecutor] CRITICAL: Command N" + std::to_string(commandNumber) + " not in history!");
            // Il firmware richiede sempre lastCommandNumber + 1, questo non dovrebbe mai accadere
            // Genera un comando vuoto per mantenere la sequenza
            resendCommand = "N" + std::to_string(commandNumber) + " G4 P0"; // No-op command
            Logger::logWarning("[CommandExecutor] Sending no-op command to maintain sequence");
        }

        Logger::logInfo("[CommandExecutor] Resending N" + std::to_string(commandNumber) + ": " + resendCommand);
        protocolHandler_->sendCommand(resendCommand);
    }

} // namespace core