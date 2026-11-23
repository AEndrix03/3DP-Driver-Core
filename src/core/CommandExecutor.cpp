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

    // ------------------------------------------------------------------
    // Finestra dei movimenti binari
    // ------------------------------------------------------------------

    CommandExecutor::Reply CommandExecutor::nextReply(std::chrono::milliseconds silence) {
        Reply reply;
        auto lastActivity = std::chrono::steady_clock::now();

        while (true) {
            if (std::chrono::steady_clock::now() - lastActivity > silence) {
                reply.kind = Reply::Kind::Silence;
                return reply;
            }

            const SerialMessage message = protocolHandler_->receiveMessage();

            if (message.code == MessageCodeType::UNAVAIABLE_SERIAL_PORT ||
                message.rawMessage == "CONN_LOST" || message.rawMessage == "SERIAL_ERROR") {
                reply.kind = Reply::Kind::PortError;
                return reply;
            }
            if (message.type == MessageType::NONE) continue;

            if (message.rawMessage.find(BOOT_BANNER) != std::string::npos ||
                (SerialProtocolHandler::isValidMessage(message) && startsWith(message.payload, "BOOT "))) {
                reply.kind = Reply::Kind::Reboot;
                reply.text = startsWith(message.payload, "BOOT ") ? message.payload.substr(5) : readBootCause();
                return reply;
            }

            lastActivity = std::chrono::steady_clock::now();
            if (startsWith(message.rawMessage, "BUSY")) continue; // il firmware lavora: non e' silenzio
            if (message.type == MessageType::INVALID) {
                Logger::logWarning("[CommandExecutor] Discarded line without valid checksum: " + message.rawMessage);
                continue;
            }
            if (message.type == MessageType::INFORMATIONAL || message.type == MessageType::CRITICAL) {
                reply.body.push_back(message.payload);
                continue;
            }

            reply.kind = Reply::Kind::Terminal;
            reply.code = message.code;
            reply.number = SerialProtocolHandler::fetchMessageCommandNumber(message);
            reply.completed = SerialProtocolHandler::fetchCompletedNumber(message);
            reply.text = message.payload;
            return reply;
        }
    }

    void CommandExecutor::popFront() {
        pendingBytes_ -= pending_.front().frame.size();
        pending_.pop_front();
    }

    void CommandExecutor::abandonWindow() {
        // Ogni frame in volo riceve comunque la sua risposta (rifiuto): si lasciano arrivare
        // tutte, cosi' non restano righe vecchie davanti al comando successivo.
        size_t expected = pending_.size();
        while (expected > 0) {
            const Reply reply = nextReply(timing_.windowQuiet);
            if (reply.kind != Reply::Kind::Terminal) break;
            --expected;
        }
        pending_.clear();
        pendingBytes_ = 0;
    }

    bool CommandExecutor::resendFrom(uint32_t expected) {
        // Le risposte dei frame successivi a quello perso (tutte E04) sono ancora in arrivo
        while (true) {
            const Reply reply = nextReply(timing_.windowQuiet);
            if (reply.kind == Reply::Kind::Silence) break;
            if (reply.kind != Reply::Kind::Terminal) return false;
        }

        // Quelli prima di `expected` il firmware li ha accettati
        while (!pending_.empty() && pending_.front().number < expected) popFront();
        if (!pending_.empty() && pending_.front().number != expected) return false; // numerazione persa

        for (const auto &move: pending_) protocolHandler_->sendRaw(move.frame);
        return true;
    }

    types::Result CommandExecutor::settleOldest() {
        types::Result result;
        result.code = types::ResultCode::Success;
        if (pending_.empty()) return result;

        const uint32_t number = pending_.front().number;
        result.commandNumber = number;
        int attempts = 1;

        const auto fail = [&](types::ResultCode code, const std::string &message) {
            result.code = code;
            result.message = message;
            return result;
        };

        while (true) {
            const Reply reply = nextReply(timing_.inactivity);

            if (reply.kind == Reply::Kind::PortError) {
                pending_.clear();
                pendingBytes_ = 0;
                return fail(types::ResultCode::Error, "Serial port not available");
            }
            if (reply.kind == Reply::Kind::Reboot) {
                pending_.clear();
                pendingBytes_ = 0;
                Logger::logError("[CommandExecutor] Firmware reboot detected (cause: " + reply.text + ")");
                return fail(types::ResultCode::SyncLost, "Firmware reboot detected (cause: " + reply.text + ")");
            }

            const bool resendRequested = reply.kind == Reply::Kind::Terminal &&
                                         (reply.code == MessageCodeType::CHECKSUM_ERROR ||
                                          reply.code == MessageCodeType::RESEND_COMMAND_ERROR);

            if (reply.kind == Reply::Kind::Silence || resendRequested) {
                // Frame corrotto, frame perso o risposta persa: si rispedisce dal primo non accettato.
                // I frame gia' accettati rispondono E03 e non vengono eseguiti due volte.
                if (++attempts > timing_.maxAttempts) {
                    pending_.clear();
                    pendingBytes_ = 0;
                    return fail(types::ResultCode::Timeout,
                                reply.kind == Reply::Kind::Silence ? "No response from firmware"
                                                                   : "Move frames rejected repeatedly");
                }
                const uint32_t expected = resendRequested && reply.number ? *reply.number : number;
                Logger::logWarning("[CommandExecutor] Resending moves from N" + std::to_string(expected));
                if (expected < number || !resendFrom(expected)) {
                    pending_.clear();
                    pendingBytes_ = 0;
                    result.commandNumber = reply.number;
                    return fail(types::ResultCode::Resend, "Firmware expects N" + std::to_string(expected));
                }
                if (pending_.empty() || pending_.front().number != number) {
                    // il piu' vecchio risulta accettato: la sua risposta era quella persa
                    return result;
                }
                continue;
            }

            if (!reply.number || *reply.number != number) continue; // risposta di un frame gia' risolto

            if (reply.completed) windowCompleted_ = reply.completed;
            result.completedNumber = windowCompleted_;
            result.body = reply.body;

            switch (reply.code) {
                case MessageCodeType::OK:
                case MessageCodeType::DUPLICATE_COMMAND_ERROR:
                    popFront();
                    result.message = "Move queued";
                    return result;
                case MessageCodeType::REJECTED_ERROR: {
                    std::string message = "Rejected by firmware";
                    for (const auto &line: reply.body) {
                        if (startsWith(line, "ERR ")) message = "Rejected by firmware: " + line.substr(4);
                    }
                    popFront();
                    abandonWindow(); // il firmware scarta anche quelli che seguono
                    return fail(types::ResultCode::Rejected, message);
                }
                case MessageCodeType::BLOCKED_MOTION_ERROR:
                case MessageCodeType::BLOCKED_TEMP_ERROR: {
                    std::string message = "Firmware is in error state";
                    for (const auto &line: reply.body) {
                        if (startsWith(line, "ERR ")) message = "Firmware is in error state: " + line.substr(4);
                    }
                    popFront();
                    abandonWindow();
                    return fail(types::ResultCode::Busy, message);
                }
                default:
                    popFront();
                    abandonWindow();
                    return fail(types::ResultCode::Error, "Unknown response: " + reply.text);
            }
        }
    }

    types::Result CommandExecutor::drainLocked() {
        types::Result result;
        result.code = types::ResultCode::Success;
        while (!pending_.empty()) {
            result = settleOldest();
            if (!result.isSuccess()) return result;
        }
        result.completedNumber = windowCompleted_;
        return result;
    }

    types::Result CommandExecutor::drainMoves() {
        std::lock_guard<std::mutex> lock(serialMutex_);
        return drainLocked();
    }

    bool CommandExecutor::hasPendingMoves() {
        std::lock_guard<std::mutex> lock(serialMutex_);
        return !pending_.empty();
    }

    std::optional<uint32_t> CommandExecutor::lastWindowCompleted() {
        std::lock_guard<std::mutex> lock(serialMutex_);
        return windowCompleted_;
    }

    types::Result CommandExecutor::submitMove(const std::string &frame, uint32_t commandNumber, bool wait) {
        std::lock_guard<std::mutex> lock(serialMutex_);

        types::Result result;
        result.code = types::ResultCode::Success;
        result.commandNumber = commandNumber;

        if (!protocolHandler_->isOpen()) {
            result.code = types::ResultCode::Error;
            result.message = "Serial port not available";
            return result;
        }

        // Spazio nella finestra: prima si risolvono i piu' vecchi
        while (pending_.size() >= WINDOW_FRAMES || pendingBytes_ + frame.size() > WINDOW_BYTES) {
            const types::Result oldest = settleOldest();
            if (!oldest.isSuccess()) return oldest; // questo frame non e' stato inviato
        }

        protocolHandler_->sendRaw(frame);
        pending_.push_back({commandNumber, frame});
        pendingBytes_ += frame.size();

        if (wait) {
            types::Result drained = drainLocked();
            if (drained.isSuccess()) drained.commandNumber = commandNumber;
            return drained;
        }
        result.completedNumber = windowCompleted_;
        result.message = "Move sent";
        return result;
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
