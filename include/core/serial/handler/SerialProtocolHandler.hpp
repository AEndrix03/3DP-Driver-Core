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
#include <condition_variable>
#include <chrono>
#include <sstream>

namespace core {

    enum class MessageType {
        STANDARD,      // OK0 N123 *67
        INFORMATIONAL, // POS 10.5 20.0 5.2 *156
        CRITICAL       // CRT TMP 205.4 200.0 *89
    };

    enum class MessageCodeType {
        OK,
        CHECKSUM_ERROR,
        CHECKSUM_ERROR_SKIP,
        BUFFER_OVERFLOW_ERROR,
        DUPLICATE_COMMAND_ERROR,
        RESEND_COMMAND_ERROR,
        INVALID_CATEGORY_ERROR,
        BLOCKED_MOTION_ERROR,
        BLOCKED_TEMP_ERROR,
        CANCELLED_ERROR,
        NO_ERR,
        UNKNOWN,
        UNAVAIABLE_SERIAL_PORT,
        EMPTY_MESSAGE,
        CRITICAL_MESSAGE_PROCESSING_ERROR,
    };

    struct SerialMessage {
        MessageType type;
        MessageCodeType code;
        std::string payload;
        uint8_t receivedChecksum;
        uint8_t calculatedChecksum;
        std::string rawMessage;
    };

    /**
     * @brief Gestisce il protocollo seriale con checksum e ACK secondo le specifiche
     */
    class SerialProtocolHandler {
    public:
        explicit SerialProtocolHandler(std::shared_ptr<SerialPort> serialPort);

        ~SerialProtocolHandler() = default;

        /**
         * @brief Riceve un messaggio dal firmware con gestione checksum e ACK
         * @return SerialMessage parsed, con validazione checksum
         */
        SerialMessage receiveMessage();

        /**
         * @brief Invia un comando al firmware (wrapper per compatibilità)
         * @param command Comando da inviare
         */
        void sendCommand(const std::string &command);

        /**
         * @brief Controlla se la porta seriale è aperta
         */
        bool isOpen() const;

        static inline MessageCodeType decodeMessageCodeFromString(const std::string &code) {
            if (code == "OK0")
                return MessageCodeType::OK;
            else if (code == "E01")
                return MessageCodeType::CHECKSUM_ERROR;
            else if (code == "E02")
                return MessageCodeType::BUFFER_OVERFLOW_ERROR;
            else if (code == "E03")
                return MessageCodeType::DUPLICATE_COMMAND_ERROR;
            else if (code == "E04")
                return MessageCodeType::RESEND_COMMAND_ERROR;
            else if (code == "E05")
                return MessageCodeType::INVALID_CATEGORY_ERROR;
            else if (code == "EM0")
                return MessageCodeType::BLOCKED_MOTION_ERROR;
            else if (code == "ET0")
                return MessageCodeType::BLOCKED_TEMP_ERROR;
            else if (code == "ES0")
                return MessageCodeType::CANCELLED_ERROR;
            else if (code == "ES1")
                return MessageCodeType::NO_ERR;
            else
                return MessageCodeType::UNKNOWN;
        }

        static bool isOk(const SerialMessage &message) {
            return message.code == MessageCodeType::OK;
        }

        static inline bool isDuplicate(const SerialMessage &message) {
            return message.code == MessageCodeType::DUPLICATE_COMMAND_ERROR;
        }

        static bool isResend(const SerialMessage &message) {
            return message.code == MessageCodeType::RESEND_COMMAND_ERROR;
        }

        static bool isChecksumMismatch(const SerialMessage &message) {
            return message.code == MessageCodeType::CHECKSUM_ERROR;
        }

        static bool isBufferOverflow(const SerialMessage &message) {
            return message.code == MessageCodeType::BUFFER_OVERFLOW_ERROR;
        }

        static bool isInvalidCategory(const SerialMessage &message) {
            return message.code == MessageCodeType::INVALID_CATEGORY_ERROR;
        }

        static bool isMotionBlocked(const SerialMessage &message) {
            return message.code == MessageCodeType::BLOCKED_MOTION_ERROR;
        }

        static bool isTemperatureBlocked(const SerialMessage &message) {
            return message.code == MessageCodeType::BLOCKED_TEMP_ERROR;
        }

        static bool isOperationCancelled(const SerialMessage &message) {
            return message.code == MessageCodeType::CANCELLED_ERROR;
        }

        static bool isNoError(const SerialMessage &message) {
            return message.code == MessageCodeType::NO_ERR;
        }

        static bool isUnknown(const SerialMessage &message) {
            return message.code == MessageCodeType::UNKNOWN;
        }

        static bool isValidMessage(const SerialMessage &message) {
            return message.receivedChecksum == message.calculatedChecksum;
        }

        static uint32_t fetchMessageCommandNumber(const SerialMessage &message) {
            std::istringstream iss(message.payload);
            std::string token;
            iss >> token;

            if (iss >> token && token.find('N') == 0) {
                try {
                    return static_cast<uint32_t>(std::stoul(token.substr(1)));
                } catch (const std::exception &e) {
                    Logger::logError("[SerialProtocolHandler] Failed to parse command number: " + token);
                }
            }

            return -1;
        }

    private:
        std::shared_ptr<SerialPort> serialPort_;
        mutable std::mutex protocolMutex_;
        std::condition_variable criticalMessageCondition_;
        bool waitingForCriticalMessage_;

        /**
         * @brief Calcola il checksum XOR di una stringa
         * @param data Stringa per cui calcolare il checksum
         * @return Checksum calcolato
         */
        uint8_t computeChecksum(const std::string &data) const;

        /**
         * @brief Estrae il checksum dalla stringa (dopo *)
         * @param message Messaggio completo
         * @return Checksum estratto, 0 se non trovato
         */
        uint8_t extractChecksum(const std::string &message) const;

        /**
         * @brief Ottiene la parte del messaggio prima del checksum
         * @param message Messaggio completo
         * @return Parte del messaggio senza " *checksum"
         */
        std::string getMessagePayload(const std::string &message) const;

        /**
         * @brief Determina il tipo di messaggio
         * @param message Messaggio ricevuto
         * @return Tipo di messaggio identificato
         */
        MessageType identifyMessageType(const std::string &message) const;

        /**
         * @brief Parsa un messaggio ricevuto
         * @param rawMessage Messaggio grezzo ricevuto
         * @return SerialMessage strutturato
         */
        SerialMessage parseMessage(const std::string &rawMessage) const;

        /**
         * @brief Invia ACK per il messaggio ricevuto
         * @param checksum Checksum da includere nell'ACK
         */
        void sendAck(uint8_t checksum);

        /**
         * @brief Gestisce messaggi critici con blocco sincronizzato
         * @param message Messaggio CRT ricevuto
         * @return SerialMessage validato o nuovo messaggio ricevuto
         */
        SerialMessage handleCriticalMessage(SerialMessage &message);

        /**
         * @brief Attende un nuovo messaggio dal firmware (per messaggi CRT con errore)
         * @return Nuovo messaggio ricevuto
         */
        SerialMessage waitForRetryMessage(long timeoutMs = 300000);
    };

} // namespace core