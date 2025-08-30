//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "core/serial/SerialPort.hpp"
#include "CommandContext.hpp"
#include "types/Result.hpp"
#include <memory>
#include <string>
#include <mutex>

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
        types::Result sendCommandAndAwaitResponse(const std::string &command, uint16_t commandNumber);

    private:
        std::shared_ptr<SerialPort> serial_;
        std::shared_ptr<CommandContext> context_;
        std::mutex serialMutex_;

        /**
         * @brief Process responses for a specific command number.
         * Handles OK, RESEND, DUPLICATE, ERROR, and BUSY responses.
         * @param expectedNumber The command number we're waiting for.
         * @return Result of the command execution.
         */
        types::Result processResponse(uint16_t expectedNumber);

        /**
         * @brief Handle a RESEND request for a specific command.
         * @param commandNumber The command number to resend.
         * @return true if command was found and resent, false otherwise.
         */
        bool handleResend(uint16_t commandNumber);
    };

} // namespace core