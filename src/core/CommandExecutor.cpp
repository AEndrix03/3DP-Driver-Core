#include "core/CommandExecutor.hpp"
#include "logger/Logger.hpp"
#include <chrono>
#include <thread>

namespace core {

    namespace {
        constexpr const char *BOOT_BANNER = "Avvio firmware 3DP";

        bool startsWith(const std::string &text, const char *prefix) {
            return text.rfind(prefix, 0) == 0;
        }
    }

    CommandExecutor::CommandExecutor(std::shared_ptr<SerialPort> serial, std::shared_ptr<CommandContext> context)
            : serial_(std::move(serial)), context_(std::move(context)) {
        protocolHandler_ = std::make_shared<SerialProtocolHandler>(serial_);
    }

    types::Result CommandExecutor::sendCommandAndAwaitResponse(const std::string &command, uint32_t commandNumber) {
        std::lock_guard<std::mutex> lock(serialMutex_);

        types::Result result;
        result.code = types::ResultCode::Error;
        result.commandNumber = commandNumber;

        for (int attempt = 1; attempt <= timing_.maxAttempts; ++attempt) {
            if (!protocolHandler_->isOpen()) {
                result.code = types::ResultCode::Error;
                result.message = "Serial port not available";
                return result;
            }

            protocolHandler_->sendCommand(command);
            result = processResponse(commandNumber);

            // Ritrasmettere lo stesso N e' sempre sicuro: se il firmware lo aveva
            // gia' eseguito risponde E03 (duplicato), altrimenti lo esegue ora.
            const bool retryable = result.isChecksumMismatch() || result.isTimeout();
            if (!retryable) {
                return result;
            }

            Logger::logWarning("[CommandExecutor] N" + std::to_string(commandNumber) + " attempt " +
                               std::to_string(attempt) + "/" + std::to_string(timing_.maxAttempts) +
                               " failed: " + result.message);

            if (attempt < timing_.maxAttempts) {
                std::this_thread::sleep_for(timing_.retryDelay);
            }
        }

        return result;
    }

    types::Result CommandExecutor::processResponse(uint32_t expectedNumber) {
        types::Result result;
        result.code = types::ResultCode::Timeout;
        result.commandNumber = expectedNumber;

        const auto start = std::chrono::steady_clock::now();
        auto lastActivity = start;

        while (true) {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastActivity > timing_.inactivity) {
                result.code = types::ResultCode::Timeout;
                result.message = "No response from firmware";
                return result;
            }
            if (now - start > timing_.absolute) {
                result.code = types::ResultCode::Timeout;
                result.message = "Command exceeded maximum duration";
                return result;
            }

            const SerialMessage message = protocolHandler_->receiveMessage();

            if (message.code == MessageCodeType::UNAVAIABLE_SERIAL_PORT ||
                message.rawMessage == "CONN_LOST" || message.rawMessage == "SERIAL_ERROR") {
                result.code = types::ResultCode::Error;
                result.message = "Serial port not available";
                return result;
            }

            if (message.type == MessageType::NONE) {
                continue; // receiveLine ha gia' atteso il proprio timeout
            }

            lastActivity = std::chrono::steady_clock::now();

            // Il firmware si e' riavviato mentre aspettavamo: tutto lo stato macchina e' perso
            if (message.rawMessage.find(BOOT_BANNER) != std::string::npos ||
                (SerialProtocolHandler::isValidMessage(message) && startsWith(message.payload, "BOOT "))) {
                std::string cause = startsWith(message.payload, "BOOT ") ? message.payload.substr(5)
                                                                           : readBootCause();
                result.code = types::ResultCode::SyncLost;
                result.message = "Firmware reboot detected (cause: " + cause + ")";
                Logger::logError("[CommandExecutor] " + result.message);
                return result;
            }

            if (startsWith(message.rawMessage, "BUSY")) {
                continue;
            }

            if (message.type == MessageType::INVALID) {
                Logger::logWarning("[CommandExecutor] Discarded line without valid checksum: " + message.rawMessage);
                continue;
            }

            if (message.type == MessageType::INFORMATIONAL || message.type == MessageType::CRITICAL) {
                result.body.push_back(message.payload);
                continue;
            }

            // ---- risposta terminale ----
            const auto number = SerialProtocolHandler::fetchMessageCommandNumber(message);

            switch (message.code) {
                case MessageCodeType::CHECKSUM_ERROR:
                    // il firmware non sa a quale comando appartiene la riga corrotta
                    result.code = types::ResultCode::ChecksumMismatch;
                    result.message = "Firmware reported checksum error";
                    return result;
                case MessageCodeType::BUFFER_OVERFLOW_ERROR:
                    result.code = types::ResultCode::BufferOverflow;
                    result.message = "Command too long for firmware buffer";
                    return result;
                case MessageCodeType::RESEND_COMMAND_ERROR:
                    // Al comando di sincronizzazione (N0) il firmware non risponde mai E04:
                    // e' la risposta rimasta in coda di un comando precedente al riavvio.
                    if (expectedNumber == 0) continue;
                    result.code = types::ResultCode::Resend;
                    result.commandNumber = number;
                    result.message = "Firmware expects N" + (number ? std::to_string(*number) : std::string("?"));
                    return result;
                default:
                    break;
            }

            if (!number || *number != expectedNumber) {
                Logger::logWarning("[CommandExecutor] Stale response discarded (waiting N" +
                                   std::to_string(expectedNumber) + "): " + message.payload);
                continue;
            }

            switch (message.code) {
                case MessageCodeType::OK:
                    result.code = types::ResultCode::Success;
                    result.message = "Command acknowledged";
                    result.completedNumber = SerialProtocolHandler::fetchCompletedNumber(message);
                    break;
                case MessageCodeType::DUPLICATE_COMMAND_ERROR:
                    // gia' eseguito: era andata persa solo la risposta
                    result.code = types::ResultCode::Duplicate;
                    result.message = "Command already executed (response was lost)";
                    break;
                case MessageCodeType::INVALID_CATEGORY_ERROR:
                    result.code = types::ResultCode::Error;
                    result.message = "Invalid command category";
                    break;
                case MessageCodeType::REJECTED_ERROR: {
                    result.code = types::ResultCode::Rejected;
                    result.message = "Rejected by firmware";
                    for (const auto &line: result.body) {
                        if (startsWith(line, "ERR ")) result.message = "Rejected by firmware: " + line.substr(4);
                    }
                    break;
                }
                case MessageCodeType::BLOCKED_MOTION_ERROR:
                case MessageCodeType::BLOCKED_TEMP_ERROR: {
                    result.code = types::ResultCode::Busy;
                    result.message = "Firmware is in error state";
                    for (const auto &line: result.body) {
                        if (startsWith(line, "ERR ")) result.message = "Firmware is in error state: " + line.substr(4);
                    }
                    break;
                }
                case MessageCodeType::CANCELLED_ERROR:
                    result.code = types::ResultCode::Success;
                    result.message = "Error cleared";
                    break;
                case MessageCodeType::NO_ERR:
                    result.code = types::ResultCode::Success;
                    result.message = "No error";
                    break;
                default:
                    result.code = types::ResultCode::Error;
                    result.message = "Unknown response: " + message.payload;
                    break;
            }
            return result;
        }
    }

    std::string CommandExecutor::readBootCause() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
        while (std::chrono::steady_clock::now() < deadline) {
            const SerialMessage message = protocolHandler_->receiveMessage();
            if (SerialProtocolHandler::isValidMessage(message) && startsWith(message.payload, "BOOT ")) {
                return message.payload.substr(5);
            }
            if (message.rawMessage.find("Sistema pronto") != std::string::npos) break;
        }
        return "UNKNOWN";
    }

} // namespace core
