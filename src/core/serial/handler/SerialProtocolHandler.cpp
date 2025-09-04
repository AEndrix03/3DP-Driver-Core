//
// Created by redeg on 02/09/2025.
//

#include "core/serial/handler/SerialProtocolHandler.hpp"
#include "logger/Logger.hpp"
#include <sstream>
#include <algorithm>
#include <regex>
#include <iomanip>

namespace core {

    SerialProtocolHandler::SerialProtocolHandler(std::shared_ptr<SerialPort> serialPort)
            : serialPort_(std::move(serialPort)), waitingForCriticalMessage_(false) {
        if (!serialPort_) {
            throw std::invalid_argument("SerialPort cannot be null");
        }
        Logger::logInfo("[SerialProtocolHandler] Initialized with checksum validation and ACK protocol");
    }

    SerialMessage SerialProtocolHandler::receiveMessage() {
        std::lock_guard<std::mutex> lock(protocolMutex_);

        if (!serialPort_ || !serialPort_->isOpen()) {
            Logger::logError("[SerialProtocolHandler] Serial port not available");
            return {MessageType::STANDARD, MessageCodeType::UNAVAIABLE_SERIAL_PORT, "", 0, 0, ""};
        }

        std::string rawMessage = serialPort_->receiveLine();
        if (rawMessage.empty()) {
            return {MessageType::STANDARD, MessageCodeType::EMPTY_MESSAGE, "", 0, 0, ""};
        }

        Logger::logInfo("[SerialProtocolHandler] Raw message received: " + rawMessage);

        SerialMessage message = parseMessage(rawMessage);

        // Invia sempre ACK per ogni messaggio ricevuto
        sendAck(message.calculatedChecksum > 0 ? message.calculatedChecksum : 0);

        // Gestione speciale per messaggi critici
        if (message.type == MessageType::CRITICAL) {
            return handleCriticalMessage(message);
        }

        // Per messaggi standard/informativi: valida e processa
        if (!isValidMessage(message)) {
            Logger::logWarning("[SerialProtocolHandler] Checksum mismatch - discarding message: " + rawMessage);
            return {MessageType::STANDARD, MessageCodeType::CHECKSUM_ERROR_SKIP, "", 0, 0, rawMessage};
        }

        Logger::logInfo("[SerialProtocolHandler] Message validated successfully");
        return message;
    }

    void SerialProtocolHandler::sendCommand(const std::string &command) {
        if (isOpen()) {
            serialPort_->send(command);
        } else {
            Logger::logError("[SerialProtocolHandler] Cannot send - serial port not available");
        }
    }

    bool SerialProtocolHandler::isOpen() const {
        return serialPort_ && serialPort_->isOpen();
    }

    uint8_t SerialProtocolHandler::computeChecksum(const std::string &data) const {
        uint8_t checksum = 0;
        for (char c: data) {
            checksum ^= static_cast<uint8_t>(c);
        }
        return checksum;
    }

    uint8_t SerialProtocolHandler::extractChecksum(const std::string &message) const {
        size_t pos = message.find(" *");
        if (pos == std::string::npos) {
            Logger::logWarning("[SerialProtocolHandler] No checksum found in message: " + message);
            return -1; // Return 0 if no checksum found
        }

        std::string checksumStr = message.substr(pos + 2);
        try {
            return static_cast<uint8_t>(std::stoi(checksumStr));
        } catch (const std::exception &e) {
            Logger::logError("[SerialProtocolHandler] Invalid checksum format: " + checksumStr);
            return -1;
        }
    }

    std::string SerialProtocolHandler::getMessagePayload(const std::string &message) const {
        size_t pos = message.find(" *");
        if (pos == std::string::npos) {
            return message; // No checksum marker found
        }

        std::string payload = message.substr(0, pos);
        // Remove trailing spaces
        while (!payload.empty() && payload.back() == ' ') {
            payload.pop_back();
        }
        return payload;
    }

    MessageType SerialProtocolHandler::identifyMessageType(const std::string &message) const {
        if (message.find("CRT ") == 0) {
            return MessageType::CRITICAL;
        }

        // Check for standard response codes (OK, E01-E05, EM0, ET0, ES0, ES1)
        std::regex standardPattern("^(OK[0-9]|E[0-9]{2}|E[MT][0-9]|ES[0-9]) N[0-9]+");
        if (std::regex_search(message, standardPattern)) {
            return MessageType::STANDARD;
        }

        // Everything else is informational (POS, TMP, etc.)
        return MessageType::INFORMATIONAL;
    }

    SerialMessage SerialProtocolHandler::parseMessage(const std::string &rawMessage) const {
        SerialMessage message;
        message.rawMessage = rawMessage;
        message.type = identifyMessageType(rawMessage);
        message.receivedChecksum = extractChecksum(rawMessage);
        std::string payload = getMessagePayload(rawMessage);
        message.payload = payload;
        message.calculatedChecksum = computeChecksum(payload);

        std::istringstream iss(payload);
        std::string token;
        iss >> token;
        message.code = decodeMessageCodeFromString(token);

        Logger::logInfo(std::string("[SerialProtocolHandler] Parsed - Type: ") +
                        (message.type == MessageType::CRITICAL ? "CRT" :
                         message.type == MessageType::STANDARD ? "STD" : "INF") +
                        ", Code: " + token +
                        ", Valid: " + (isValidMessage(message) ? "true" : "false") +
                        ", Checksum: " + std::to_string(message.receivedChecksum) + "/" +
                        std::to_string(message.calculatedChecksum));
        return message;
    }

    void SerialProtocolHandler::sendAck(uint8_t checksum) {
        if (!isOpen()) {
            Logger::logError("[SerialProtocolHandler] Cannot send ACK - serial port not available");
            return;
        }

        // Format: A<checksum> (3 digits, zero-padded)
        std::ostringstream ack;
        ack << "A" << std::setfill('0') << std::setw(3) << static_cast<int>(checksum);

        std::string ackMessage = ack.str();
        serialPort_->send(ackMessage);

        Logger::logInfo("[SerialProtocolHandler] Sent ACK: " + ackMessage);
    }

    SerialMessage SerialProtocolHandler::handleCriticalMessage(SerialMessage &message) {
        Logger::logInfo("[SerialProtocolHandler] Handling critical message: " + message.rawMessage);

        if (isValidMessage(message)) {
            Logger::logInfo("[SerialProtocolHandler] Critical message valid - processing");
            return message;
        }

        // Checksum errato per messaggio CRT - blocco e attendo nuovo messaggio
        Logger::logWarning("[SerialProtocolHandler] Critical message checksum invalid - waiting for retry");
        waitingForCriticalMessage_ = true;

        SerialMessage retryMessage = waitForRetryMessage();
        waitingForCriticalMessage_ = false;

        return retryMessage;
    }

    SerialMessage SerialProtocolHandler::waitForRetryMessage(long timeoutMs) {
        Logger::logInfo("[SerialProtocolHandler] Waiting for firmware retry...");

        auto startTime = std::chrono::steady_clock::now();
        auto timeout = std::chrono::milliseconds(timeoutMs);

        while (std::chrono::steady_clock::now() - startTime < timeout) {
            if (!isOpen()) {
                Logger::logError("[SerialProtocolHandler] Serial port lost during retry wait");
                return {MessageType::CRITICAL, MessageCodeType::UNAVAIABLE_SERIAL_PORT, "", 0, 0, ""};
            }

            std::string rawMessage = serialPort_->receiveLine();
            if (!rawMessage.empty()) {
                Logger::logInfo("[SerialProtocolHandler] Retry message received: " + rawMessage);

                SerialMessage retryMessage = parseMessage(rawMessage);

                // Invia ACK per il nuovo messaggio
                sendAck(retryMessage.calculatedChecksum > 0 ? retryMessage.calculatedChecksum : 0);

                if (isValidMessage(retryMessage)) {
                    Logger::logInfo("[SerialProtocolHandler] Retry message valid - unblocking");
                    return retryMessage;
                } else {
                    Logger::logWarning("[SerialProtocolHandler] Retry message still invalid - continuing wait");
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        Logger::logError("[SerialProtocolHandler] Timeout waiting for valid retry message");
        return {MessageType::CRITICAL, MessageCodeType::CRITICAL_MESSAGE_PROCESSING_ERROR, "", 0, 0,
                "UNAVAIABLE_SERIAL_PORT"};
    }

} // namespace core