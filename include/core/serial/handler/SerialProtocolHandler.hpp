//
// Created by redeg on 02/09/2025.
//

#pragma once

#include "core/serial/SerialPort.hpp"
#include "core/types/Result.hpp"
#include "logger/Logger.hpp"
#include <memory>
#include <string>
#include <mutex>
#include <chrono>
#include <sstream>
#include <optional>

namespace core {

    enum class MessageType {
        STANDARD,      // OK0 N123 *67            risposta terminale di un comando
        INFORMATIONAL, // POS 10.5 20.0 5.2 *156  dato
        CRITICAL,      // CRT TMP 205.4 200.0 *89 dato da confermare con ACK
        INVALID,       // checksum assente o errato: da scartare
        NONE           // nessuna riga ricevuta entro il timeout di lettura
    };

    enum class MessageCodeType {
        OK,
        CHECKSUM_ERROR,
        BUFFER_OVERFLOW_ERROR,
        DUPLICATE_COMMAND_ERROR,
        RESEND_COMMAND_ERROR,
        INVALID_CATEGORY_ERROR,
        REJECTED_ERROR,
        BLOCKED_MOTION_ERROR,
        BLOCKED_TEMP_ERROR,
        CANCELLED_ERROR,
        NO_ERR,
        UNKNOWN,
        UNAVAIABLE_SERIAL_PORT,
    };

    struct SerialMessage {
        MessageType type = MessageType::NONE;
        MessageCodeType code = MessageCodeType::UNKNOWN;
        std::string payload;                  // riga senza " *checksum"
        std::optional<uint8_t> receivedChecksum;
        uint8_t calculatedChecksum = 0;
        std::string rawMessage;
    };

    /**
     * @brief Livello di protocollo: checksum in ingresso e ACK dei messaggi critici.
     *
     * L'ACK ("A<checksum a 3 cifre>") viene inviato SOLO per i messaggi CRT: sono gli
     * unici che il firmware attende. Confermare ogni riga (BUSY compresi) riempiva il
     * buffer RX da 64 byte dell'Arduino durante i movimenti lunghi, corrompendo il
     * comando successivo.
     */
    class SerialProtocolHandler {
    public:
        explicit SerialProtocolHandler(std::shared_ptr<SerialPort> serialPort);

        ~SerialProtocolHandler() = default;

        /**
         * @brief Riceve una riga dal firmware. Non blocca oltre il timeout di lettura della porta.
         * @return messaggio; type == NONE se non e' arrivato nulla
         */
        SerialMessage receiveMessage();

        /**
         * @brief Invia un comando al firmware
         */
        void sendCommand(const std::string &command);

        /**
         * @brief Controlla se la porta seriale è aperta
         */
        bool isOpen() const;

        static MessageCodeType decodeMessageCodeFromString(const std::string &code) {
            if (code == "OK0") return MessageCodeType::OK;
            if (code == "E01") return MessageCodeType::CHECKSUM_ERROR;
            if (code == "E02") return MessageCodeType::BUFFER_OVERFLOW_ERROR;
            if (code == "E03") return MessageCodeType::DUPLICATE_COMMAND_ERROR;
            if (code == "E04") return MessageCodeType::RESEND_COMMAND_ERROR;
            if (code == "E05") return MessageCodeType::INVALID_CATEGORY_ERROR;
            if (code == "E06") return MessageCodeType::REJECTED_ERROR;
            if (code == "EM0") return MessageCodeType::BLOCKED_MOTION_ERROR;
            if (code == "ET0") return MessageCodeType::BLOCKED_TEMP_ERROR;
            if (code == "ES0") return MessageCodeType::CANCELLED_ERROR;
            if (code == "ES1") return MessageCodeType::NO_ERR;
            return MessageCodeType::UNKNOWN;
        }

        static bool isValidMessage(const SerialMessage &message) {
            return message.receivedChecksum.has_value() &&
                   *message.receivedChecksum == message.calculatedChecksum;
        }

        /**
         * @brief Numero comando ("N123") di una risposta terminale.
         */
        static std::optional<uint32_t> fetchMessageCommandNumber(const SerialMessage &message) {
            std::istringstream iss(message.payload);
            std::string token;
            iss >> token;

            if (iss >> token && token.size() > 1 && token[0] == 'N') {
                try {
                    return static_cast<uint32_t>(std::stoul(token.substr(1)));
                } catch (const std::exception &) {
                }
            }
            return std::nullopt;
        }

        /**
         * @brief Campo "D<numero>" di una risposta OK (ultimo comando completato).
         */
        static std::optional<uint32_t> fetchCompletedNumber(const SerialMessage &message) {
            std::istringstream iss(message.payload);
            std::string token;
            while (iss >> token) {
                if (token.size() > 1 && token[0] == 'D') {
                    try {
                        return static_cast<uint32_t>(std::stoul(token.substr(1)));
                    } catch (const std::exception &) {
                    }
                }
            }
            return std::nullopt;
        }

    private:
        std::shared_ptr<SerialPort> serialPort_;
        mutable std::mutex protocolMutex_;

        static uint8_t computeChecksum(const std::string &data);

        static MessageType identifyMessageType(const std::string &payload);

        SerialMessage parseMessage(const std::string &rawMessage) const;

        void sendAck(uint8_t checksum);
    };

} // namespace core
