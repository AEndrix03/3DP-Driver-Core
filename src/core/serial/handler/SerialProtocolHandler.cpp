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
            return {MessageType::STANDARD, "", "", "", 0, 0, false, ""};
        }

        std::string rawMessage = serialPort_->receiveLine();
        if (rawMessage.empty()) {
            return {MessageType::STANDARD, "", "", "", 0, 0, false, ""};
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
        if (!message.isValid) {
            Logger::logWarning("[SerialProtocolHandler] Checksum mismatch - discarding message: " + rawMessage);
            // Per messaggi non critici, scarta e continua
            return {MessageType::STANDARD, "", "", "", 0, 0, false, rawMessage};
        }

        Logger::logInfo("[SerialProtocolHandler] Message validated successfully: " + message.code);
        return message;
    }

    void SerialProtocolHandler::sendCommand(const std::string &command) {
        if (serialPort_ && serialPort_->isOpen()) {
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
            return 0; // Return 0 if no checksum found
        }

        std::string checksumStr = message.substr(pos + 2);
        try {
            return static_cast<uint8_t>(std::stoi(checksumStr));
        } catch (const std::exception &e) {
            Logger::logError("[SerialProtocolHandler] Invalid checksum format: " + checksumStr);
            return 0;
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
        message.isDuplicate = false;
        message.isResendRequest = false;
        message.resendCommandNumber = 0;

        // Extract checksum
        message.receivedChecksum = extractChecksum(rawMessage);

        // Get payload (message without checksum)
        std::string payload = getMessagePayload(rawMessage);
        message.payload = payload;

        // Calculate expected checksum
        message.calculatedChecksum = computeChecksum(payload);

        // Validate checksum
        message.isValid = (message.receivedChecksum == message.calculatedChecksum);

        // Parse components based on message type
        std::istringstream iss(payload);
        std::string token;
        iss >> token; // First token is the code
        message.code = token;

        // Extract command number if present (N123 format)
        if (message.type == MessageType::STANDARD) {
            if (iss >> token && token.find("N") == 0) {
                message.commandNumber = token;

                // Parse command number for E03/E04 handling
                try {
                    uint16_t cmdNum = static_cast<uint16_t>(std::stoul(token.substr(1)));

                    // Handle specific error codes
                    if (message.code == "E03") {
                        message.isDuplicate = true;
                        Logger::logInfo("[SerialProtocolHandler] DUPLICATE detected for N" + std::to_string(cmdNum));
                    } else if (message.code == "E04") {
                        message.isResendRequest = true;
                        message.resendCommandNumber = cmdNum;
                        Logger::logInfo("[SerialProtocolHandler] RESEND requested for N" + std::to_string(cmdNum));
                    }
                } catch (const std::exception &e) {
                    Logger::logWarning("[SerialProtocolHandler] Failed to parse command number: " + token);
                }
            }
        }

        Logger::logInfo(std::string("[SerialProtocolHandler] Parsed - Type: ") +
                        (message.type == MessageType::CRITICAL ? "CRT" :
                         message.type == MessageType::STANDARD ? "STD" : "INF") +
                        ", Code: " + message.code +
                        ", Valid: " + (message.isValid ? "true" : "false") +
                        ", Checksum: " + std::to_string(message.receivedChecksum) + "/" +
                        std::to_string(message.calculatedChecksum));
        return message;
    }

    void SerialProtocolHandler::sendAck(uint8_t checksum) {
        if (!serialPort_ || !serialPort_->isOpen()) {
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

    SerialMessage SerialProtocolHandler::handleCriticalMessage(const SerialMessage &message) {
        Logger::logInfo("[SerialProtocolHandler] Handling critical message: " + message.rawMessage);

        if (message.isValid) {
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

    SerialMessage SerialProtocolHandler::waitForRetryMessage(int timeoutMs) {
        Logger::logInfo("[SerialProtocolHandler] Waiting for firmware retry...");

        auto startTime = std::chrono::steady_clock::now();
        auto timeout = std::chrono::milliseconds(timeoutMs);

        while (std::chrono::steady_clock::now() - startTime < timeout) {
            if (!serialPort_ || !serialPort_->isOpen()) {
                Logger::logError("[SerialProtocolHandler] Serial port lost during retry wait");
                break;
            }

            std::string rawMessage = serialPort_->receiveLine();
            if (!rawMessage.empty()) {
                Logger::logInfo("[SerialProtocolHandler] Retry message received: " + rawMessage);

                SerialMessage retryMessage = parseMessage(rawMessage);

                // Invia ACK per il nuovo messaggio
                sendAck(retryMessage.calculatedChecksum > 0 ? retryMessage.calculatedChecksum : 0);

                if (retryMessage.isValid) {
                    Logger::logInfo("[SerialProtocolHandler] Retry message valid - unblocking");
                    return retryMessage;
                } else {
                    Logger::logWarning("[SerialProtocolHandler] Retry message still invalid - continuing wait");
                    // Continue waiting for valid message
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        Logger::logError("[SerialProtocolHandler] Timeout waiting for valid retry message");
        return {MessageType::CRITICAL, "", "", "", 0, 0, false, "TIMEOUT"};
    }

} // namespace core