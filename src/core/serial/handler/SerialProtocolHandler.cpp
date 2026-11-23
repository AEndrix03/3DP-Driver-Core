//
// Created by redeg on 02/09/2025.
//

#include "core/serial/handler/SerialProtocolHandler.hpp"
#include "logger/Logger.hpp"
#include <sstream>
#include <iomanip>
#include <cctype>
#include <stdexcept>

namespace core {

    SerialProtocolHandler::SerialProtocolHandler(std::shared_ptr<SerialPort> serialPort)
            : serialPort_(std::move(serialPort)) {
        if (!serialPort_) {
            throw std::invalid_argument("SerialPort cannot be null");
        }
    }

    SerialMessage SerialProtocolHandler::receiveMessage() {
        std::lock_guard<std::mutex> lock(protocolMutex_);

        if (!isOpen()) {
            SerialMessage unavailable;
            unavailable.code = MessageCodeType::UNAVAIABLE_SERIAL_PORT;
            return unavailable;
        }

        std::string rawMessage = serialPort_->receiveLine();
        if (rawMessage.empty()) {
            return {};
        }

        SerialMessage message = parseMessage(rawMessage);

        if (message.type == MessageType::CRITICAL) {
            // Sempre il checksum calcolato: se la riga e' arrivata corrotta il
            // firmware vede un ACK diverso e ritrasmette subito.
            sendAck(message.calculatedChecksum);
        }

        if (!isValidMessage(message)) {
            if (message.type == MessageType::CRITICAL) {
                Logger::logWarning("[SerialProtocolHandler] Critical message corrupted, waiting for retry: " +
                                   rawMessage);
            }
            message.type = MessageType::INVALID;
        }

        return message;
    }

    void SerialProtocolHandler::sendCommand(const std::string &command) {
        if (isOpen()) {
            serialPort_->send(command);
        } else {
            Logger::logError("[SerialProtocolHandler] Cannot send - serial port not available");
        }
    }

    void SerialProtocolHandler::sendRaw(const std::string &bytes) {
        if (isOpen()) serialPort_->sendRaw(bytes);
    }

    bool SerialProtocolHandler::isOpen() const {
        return serialPort_ && serialPort_->isOpen();
    }

    uint8_t SerialProtocolHandler::computeChecksum(const std::string &data) {
        uint8_t checksum = 0;
        for (char c: data) {
            checksum ^= static_cast<uint8_t>(c);
        }
        return checksum;
    }

    MessageType SerialProtocolHandler::identifyMessageType(const std::string &payload) {
        if (payload.rfind("CRT ", 0) == 0) {
            return MessageType::CRITICAL;
        }

        // Risposta terminale: "<3 caratteri di codice> N<numero>"
        if (payload.size() >= 6 && payload[3] == ' ' && payload[4] == 'N' &&
            std::isdigit(static_cast<unsigned char>(payload[5])) &&
            decodeMessageCodeFromString(payload.substr(0, 3)) != MessageCodeType::UNKNOWN) {
            return MessageType::STANDARD;
        }

        return MessageType::INFORMATIONAL;
    }

    SerialMessage SerialProtocolHandler::parseMessage(const std::string &rawMessage) const {
        SerialMessage message;
        message.rawMessage = rawMessage;

        const size_t pos = rawMessage.rfind(" *");
        std::string payload = (pos == std::string::npos) ? rawMessage : rawMessage.substr(0, pos);
        while (!payload.empty() && payload.back() == ' ') {
            payload.pop_back();
        }

        if (pos != std::string::npos) {
            const std::string checksumStr = rawMessage.substr(pos + 2);
            const bool numeric = !checksumStr.empty() && checksumStr.size() <= 3 &&
                                 checksumStr.find_first_not_of("0123456789") == std::string::npos;
            if (numeric) {
                const int value = std::stoi(checksumStr);
                if (value <= 255) {
                    message.receivedChecksum = static_cast<uint8_t>(value);
                }
            }
        }

        message.payload = payload;
        message.calculatedChecksum = computeChecksum(payload);
        message.type = identifyMessageType(payload);

        if (message.type == MessageType::STANDARD) {
            message.code = decodeMessageCodeFromString(payload.substr(0, 3));
        }

        return message;
    }

    void SerialProtocolHandler::sendAck(uint8_t checksum) {
        // Formato: A<checksum> (3 cifre con zeri iniziali)
        std::ostringstream ack;
        ack << "A" << std::setfill('0') << std::setw(3) << static_cast<int>(checksum);
        serialPort_->send(ack.str());
    }

} // namespace core
