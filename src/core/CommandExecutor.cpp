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
        return sendCommandAndAwaitResponseLocked(command, commandNumber);
    }

    types::Result
    CommandExecutor::sendCommandAndAwaitResponseLocked(const std::string &command, uint32_t commandNumber) {
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
                context_->setCommandNumber(result.commandNumber.value() - 1);
                return types::Result::resendError(result.commandNumber.value());
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            sendCommandAndAwaitResponseLocked(resendCommand, result.commandNumber.value());
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            return sendCommandAndAwaitResponseLocked(command,
                                                     commandNumber); // Esegue nuovamente a prescindere dal result
        } else if (result.isChecksumMismatch()) {
            // Se il checksum non corrisponde, riesegue il comando: se era già stato eseguito arriverà DUPLICATE, se è stato saltato un comando arriverà RESEND, altrimenti OK
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            return sendCommandAndAwaitResponseLocked(command, commandNumber);
        } else if (result.isSuccess() && result.commandNumber.has_value()) {
            context_->removeCommand(commandNumber);
            Logger::logInfo("[CommandExecutor] SET Command N" + std::to_string(result.commandNumber.value()) +
                            " completed successfully");
            context_->setCommandNumber(result.commandNumber.value() + 1);
            return result;
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
                } else {
                    result.commandNumber = SerialProtocolHandler::fetchMessageCommandNumber(message);
                    if (SerialProtocolHandler::isDuplicate(message)) {
                        Logger::logInfo("[CommandExecutor] DUPLICATE response");
                        result.code = types::ResultCode::Duplicate;
                        result.message = "Command already processed";
                    } else if (SerialProtocolHandler::isResend(message)) {
                        Logger::logWarning("[CommandExecutor] RESEND request for N" +
                                           std::to_string(result.commandNumber.value()));
                        result.code = types::ResultCode::Resend;
                        result.message = "Resend command";
                    } else if (SerialProtocolHandler::isChecksumMismatch(message)) {
                        Logger::logWarning("[CommandExecutor] Firmware checksum error");
                        result.code = types::ResultCode::ChecksumMismatch;
                        result.message = "Firmware reported checksum error";
                    } else if (SerialProtocolHandler::isBufferOverflow(message)) {
                        Logger::logError("[CommandExecutor] Firmware buffer overflow");
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    } else if (SerialProtocolHandler::isInvalidCategory(message)) {
                        Logger::logError("[CommandExecutor] Invalid command category");
                        result.code = types::ResultCode::Error;
                        result.message = "Invalid command category";
                    } else if (SerialProtocolHandler::isMotionBlocked(message)) {
                        Logger::logWarning("[CommandExecutor] Motion blocked by firmware");
                        result.code = types::ResultCode::Busy;
                        result.message = "Motion blocked";
                    } else if (SerialProtocolHandler::isTemperatureBlocked(message)) {
                        Logger::logWarning("[CommandExecutor] Temperature operation blocked");
                        result.code = types::ResultCode::Busy;
                        result.message = "Temperature blocked";
                    } else if (SerialProtocolHandler::isOperationCancelled(message)) {
                        Logger::logInfo("[CommandExecutor] Operation cancelled by firmware");
                        result.code = types::ResultCode::Skip;
                        result.message = "Operation cancelled";
                    } else if (SerialProtocolHandler::isNoError(message)) { // No error
                        Logger::logInfo("[CommandExecutor] No error response");
                        result.code = types::ResultCode::Success;
                        result.message = "No error";
                    } else {
                        Logger::logWarning("[CommandExecutor] Unknown response");
                        result.code = types::ResultCode::Error;
                        result.message = "Unknown response - continuing";
                    }
                }
                return result;
            }
        }

        return result;
    }

} // namespace core