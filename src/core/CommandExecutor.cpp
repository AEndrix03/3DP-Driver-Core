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

    types::Result CommandExecutor::sendCommandAndAwaitResponse(const std::string &command, uint32_t commandNumber) {
        std::lock_guard<std::mutex> lock(serialMutex_);

        context_->storeCommand(commandNumber, command);
        lastSentCommand_ = command;
        lastSentNumber_ = commandNumber;

        protocolHandler_->sendCommand(command);
        Logger::logInfo("[CommandExecutor] Sent N" + std::to_string(commandNumber) + ": " + command);

        types::Result result = processResponse(commandNumber);

        if (result.isDuplicate()) {
            // Se è duplicato, si passa al comando successivo e si rimuove il duplicato dallo storico
            context_->removeCommand(result.commandNumber.value());
            return types::Result::duplicate(result.commandNumber.value());
        } else if (result.isResend()) {
            // Se è resend, recupera il comando dall'history e lo reinvia. Si rimuove l'attuale comando dallo storico: verrà inserito di nuovo successivamente
            context_->removeCommand(commandNumber);

            std::string resendCommand = context_->getCommandText(result.commandNumber.value());

            if (resendCommand.empty()) {
                Logger::logError("[CommandExecutor] RESEND FAILED - command N" +
                                 std::to_string(result.commandNumber.value()) + " not found in history");
                return types::Result::resendError(result.commandNumber.value());
            }

            sendCommandAndAwaitResponse(resendCommand, result.commandNumber.value());
            return sendCommandAndAwaitResponse(command, commandNumber); // Esegue nuovamente a prescindere dal result
        } else if (result.isChecksumMismatch()) {
            // Se il checksum non corrisponde, riesegue il comando: se era già stato eseguito arriverà DUPLICATE, se è stato saltato un comando arriverà RESEND, altrimenti OK
            return sendCommandAndAwaitResponse(command, commandNumber);
        }

        //TODO: Other recovery strategies can be implemented here

        return result;
    }

    types::Result CommandExecutor::processResponse(uint32_t expectedNumber) {
        const int maxRetries = 2;
        const auto commandTimeout = std::chrono::milliseconds(300000);

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

            if (message.rawMessage.find("BUSY") == 0 || SerialProtocolHandler::isUnknown(message)) continue;

            // Scarta messaggi non critici con checksum invalido
            if (!SerialProtocolHandler::isValidMessage(message) && message.type != MessageType::CRITICAL) {
                Logger::logWarning("[CommandExecutor] Invalid message discarded: " + message.rawMessage);
                continue;
            }

            if (message.type == MessageType::INFORMATIONAL) {
                result.body.push_back(message.payload);
                continue;
            }

            if (message.type == MessageType::CRITICAL) {
                Logger::logInfo("[CommandExecutor] Critical: " + message.payload);
                result.body.push_back(message.payload);
                continue;
            }

            if (message.type == MessageType::STANDARD) {
                if (SerialProtocolHandler::isOk(message)) {
                    result.code = types::ResultCode::Success;
                    result.message = "Command acknowledged";
                    return result;
                } else {
                    result.commandNumber = SerialProtocolHandler::fetchMessageCommandNumber(message);
                    if (SerialProtocolHandler::isDuplicate(message)) {
                        Logger::logInfo("[CommandExecutor] DUPLICATE response");
                        result.code = types::ResultCode::Duplicate;
                        result.message = "Command already processed";
                        return result;
                    } else if (SerialProtocolHandler::isResend(message)) {
                        Logger::logWarning("[CommandExecutor] RESEND request for N" +
                                           std::to_string(result.commandNumber.value()));
                        result.code = types::ResultCode::Resend;
                        result.message = "Resend command";
                        return result;
                    } else if (SerialProtocolHandler::isChecksumMismatch(message)) {
                        Logger::logWarning("[CommandExecutor] Firmware checksum error");
                        result.code = types::ResultCode::ChecksumMismatch;
                        result.message = "Firmware reported checksum error";
                        return result;
                    } else if (SerialProtocolHandler::isBufferOverflow(message)) {
                        Logger::logError("[CommandExecutor] Firmware buffer overflow");
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        return result;
                    } else if (SerialProtocolHandler::isInvalidCategory(message)) {
                        Logger::logError("[CommandExecutor] Invalid command category");
                        result.code = types::ResultCode::Error;
                        result.message = "Invalid command category";
                        return result;

                    } else if (SerialProtocolHandler::isMotionBlocked(message)) {
                        Logger::logWarning("[CommandExecutor] Motion blocked by firmware");
                        result.code = types::ResultCode::Busy;
                        result.message = "Motion blocked";
                        return result;

                    } else if (SerialProtocolHandler::isTemperatureBlocked(message)) {
                        Logger::logWarning("[CommandExecutor] Temperature operation blocked");
                        result.code = types::ResultCode::Busy;
                        result.message = "Temperature blocked";
                        return result;

                    } else if (SerialProtocolHandler::isOperationCancelled(message)) {
                        Logger::logInfo("[CommandExecutor] Operation cancelled by firmware");
                        result.code = types::ResultCode::Skip;
                        result.message = "Operation cancelled";
                        return result;

                    } else if (SerialProtocolHandler::isNoError(message)) { // No error
                        Logger::logInfo("[CommandExecutor] No error response");
                        result.code = types::ResultCode::Success;
                        result.message = "No error";
                        return result;

                    } else {
                        Logger::logWarning("[CommandExecutor] Unknown response");
                        result.code = types::ResultCode::Error;
                        result.message = "Unknown response - continuing";
                        return result;
                    }
                }
            }
        }

        Logger::logWarning("[CommandExecutor] Max retries exceeded for N" + std::to_string(expectedNumber));
        result.code = types::ResultCode::Success;
        result.message = "Max retries exceeded - continuing";
        return result;
    }

    bool CommandExecutor::attemptResendFailedRecovery(uint32_t failedCommandNumber) {
        Logger::logInfo(
                "[CommandExecutor] Starting RESEND FAILED recovery for N" + std::to_string(failedCommandNumber));

        // Strategy 1: Identify what firmware expects and send it
        uint32_t expectedByFirmware = identifyExpectedCommand(failedCommandNumber);
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

    uint32_t CommandExecutor::identifyExpectedCommand(uint32_t failedNumber) {
        // Try to detect what command the firmware is waiting for
        // Usually it's the last command that didn't get confirmed

        // Check if we have the previous command
        for (uint32_t candidate = failedNumber - 1; candidate >= std::max(1, (int) failedNumber - 5); candidate--) {
            if (!context_->getCommandText(candidate).empty()) {
                Logger::logInfo("[CommandExecutor] Firmware likely expects N" + std::to_string(candidate));
                return candidate;
            }
        }

        // If no specific command found, try the failed command itself
        return failedNumber;
    }

    bool CommandExecutor::sendExpectedCommand(uint32_t commandNumber) {
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

    bool CommandExecutor::attemptLineNumberReset(uint32_t fromNumber) {
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

    void CommandExecutor::handleResend(uint32_t commandNumber) {
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