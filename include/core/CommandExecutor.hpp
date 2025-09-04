//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "core/serial/SerialPort.hpp"
#include "CommandContext.hpp"
#include "types/Result.hpp"
#include "core/serial/handler/SerialProtocolHandler.hpp"
#include <memory>
#include <string>
#include <mutex>
#include <cstdint>

namespace core {

/**
 * @brief Gestisce invio dei comandi, attesa di ACK/ERR, gestione RESEND.
 *
 * CRITICAL: This class implements strict command sequencing.
 * No new commands can be sent until the current command receives OK.
 * RESEND requests block all other operations until resolved.
 */
    class CommandExecutor {
    public:
        /**
         * @brief Costruttore.
         * @param serial Porta seriale da usare.
         * @param context Contesto dei comandi (numerazione e storico).
         */
        CommandExecutor(std::shared_ptr<SerialPort> serial, std::shared_ptr<CommandContext> context);

        /**
         * @brief Invia un comando e attende una risposta valida.
         * Blocks until command receives OK or max retries exceeded.
         * @param command Testo del comando da inviare.
         * @param commandNumber Numero del comando associato.
         * @return Esito dell'operazione.
         */
        types::Result sendCommandAndAwaitResponse(const std::string &command, uint32_t commandNumber);

        /**
         * @brief Resend the last command (for health recovery).
         * Used by health monitor to break stalls.
         */
        void resendLastCommand();

    private:
        std::shared_ptr<SerialPort> serial_;
        std::shared_ptr<CommandContext> context_;
        std::mutex serialMutex_;
        std::shared_ptr<SerialProtocolHandler> protocolHandler_;

        // Track last sent command for recovery
        std::string lastSentCommand_;
        uint32_t lastSentNumber_ = 0;
        bool firmwareSyncLost_ = false;

        /**
         * @brief Process responses for a specific command number.
         * Handles OK, RESEND, DUPLICATE, ERROR, and BUSY responses.
         * @param expectedNumber The command number we're waiting for.
         * @return Result of the command execution.
         */
        types::Result processResponse(uint32_t expectedNumber);

        /**
         * @brief Handle a RESEND request for a specific command.
         * @param commandNumber The command number to resend.
         * @return true if command was found and resent, false otherwise.
         */
        void handleResend(uint32_t commandNumber);

        /**
         * @brief Comprehensive recovery strategy for RESEND FAILED
         * @param failedCommandNumber Command that triggered RESEND FAILED
         * @return true if recovery successful, false otherwise
         */
        bool attemptResendFailedRecovery(uint32_t failedCommandNumber);

        /**
         * @brief Identify which command the firmware expects
         * @param failedNumber Command number that failed
         * @return Command number firmware likely expects
         */
        uint32_t identifyExpectedCommand(uint32_t failedNumber);

        /**
         * @brief Send the specific command firmware expects
         * @param commandNumber Command to send
         * @return true if command was sent, false if not found
         */
        bool sendExpectedCommand(uint32_t commandNumber);

        /**
         * @brief Reset line numbering using M110
         * @param fromNumber Starting number for reset
         * @return true if reset command sent
         */
        bool attemptLineNumberReset(uint32_t fromNumber);

        /**
         * @brief Full firmware reset as last resort
         * @return true if reset attempted
         */
        bool attemptFullReset();

        /**
         * @brief Wait for confirmation that recovery worked
         * @return true if firmware responds positively
         */
        bool waitForRecoveryConfirmation();

        /**
         * @brief Compute XOR checksum for command
         * @param data Command data for checksum calculation
         * @return Checksum value
         */
        uint8_t computeChecksum(const std::string &data);

        /**
         * @brief Proactive firmware sync check and recovery
         * @return true if firmware is responsive
         */
        bool attemptFirmwareResync();
    };

} // namespace core