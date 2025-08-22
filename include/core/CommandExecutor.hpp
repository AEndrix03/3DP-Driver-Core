//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "core/serial/SerialPort.hpp"
#include "CommandContext.hpp"
#include "types/Result.hpp"
#include <memory>
#include <string>

namespace core {

/**
 * @brief Gestisce invio dei comandi, attesa di ACK/ERR, gestione RESEND.
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
         * @param printer-command Testo del comando da inviare.
         * @param commandNumber Numero del comando associato.
         * @return Esito dell'operazione.
         */
        types::Result sendCommandAndAwaitResponse(const std::string &command, uint16_t commandNumber);

    private:
        std::shared_ptr<SerialPort> serial_;
        std::shared_ptr<CommandContext> context_;

        types::Result parseResponse(const std::string &response, uint16_t expectedNumber);
    };

} // namespace core
