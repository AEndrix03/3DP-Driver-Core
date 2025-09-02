//
// Created by redeg on 02/09/2025.
//

#pragma once

#include "core/serial/SerialPort.hpp"
#include "core/types/Result.hpp"
#include <memory>
#include <string>
#include <mutex>
#include <condition_variable>
#include <chrono>

namespace core {

    enum class MessageType {
        STANDARD,      // OK0 N123 *67
        INFORMATIONAL, // POS 10.5 20.0 5.2 *156
        CRITICAL       // CRT TMP 205.4 200.0 *89
    };

    struct SerialMessage {
        MessageType type;
        std::string code;           // OK0, E01, POS, TMP, etc.
        std::string commandNumber;  // N123 (se presente)
        std::string payload;        // Dati completi
        uint8_t receivedChecksum;
        uint8_t calculatedChecksum;
        bool isValid;
        std::string rawMessage;
        uint16_t resendCommandNumber; // Per E04 - comando da reinviare
        bool isDuplicate;           // Per E03
        bool isResendRequest;       // Per E04
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
        SerialMessage handleCriticalMessage(const SerialMessage &message);

        /**
         * @brief Attende un nuovo messaggio dal firmware (per messaggi CRT con errore)
         * @param timeoutMs Timeout in millisecondi
         * @return Nuovo messaggio ricevuto
         */
        SerialMessage waitForRetryMessage(int timeoutMs = 5000);
    };

} // namespace core