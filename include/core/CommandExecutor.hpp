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
#include <chrono>
#include <cstdint>

namespace core {

/**
 * @brief Invio di un comando e attesa della sua (unica) risposta terminale.
 *
 * Protocollo stop-and-wait: nessun nuovo comando finche' quello in corso non ha
 * ricevuto la risposta con lo stesso numero N. Garanzie:
 *  - un timeout NON e' mai un successo: dopo i tentativi ritorna ResultCode::Timeout;
 *  - le risposte con N diverso da quello atteso sono residui e vengono scartate;
 *  - il banner di avvio del firmware durante l'attesa ritorna ResultCode::SyncLost;
 *  - le ritrasmissioni sono limitate (niente ricorsione infinita).
 */
    class CommandExecutor {
    public:
        struct Timing {
            /// Silenzio massimo del firmware (manda BUSY ogni secondo mentre lavora)
            std::chrono::milliseconds inactivity{6000};
            /// Durata massima assoluta di un singolo comando
            std::chrono::milliseconds absolute{std::chrono::minutes(30)};
            /// Pausa prima di una ritrasmissione
            std::chrono::milliseconds retryDelay{100};
            /// Invii massimi dello stesso comando (checksum errato o timeout)
            int maxAttempts = 3;
        };

        CommandExecutor(std::shared_ptr<SerialPort> serial, std::shared_ptr<CommandContext> context);

        /**
         * @brief Invia un comando e attende la risposta terminale.
         * @param command Testo completo del comando (con N e checksum).
         * @param commandNumber Numero del comando.
         */
        types::Result sendCommandAndAwaitResponse(const std::string &command, uint32_t commandNumber);

        void setTiming(const Timing &timing) { timing_ = timing; }

        const Timing &timing() const { return timing_; }

    private:
        std::shared_ptr<SerialPort> serial_;
        std::shared_ptr<CommandContext> context_;
        std::mutex serialMutex_;
        std::shared_ptr<SerialProtocolHandler> protocolHandler_;
        Timing timing_;

        /**
         * @brief Legge finche' non arriva la risposta terminale di expectedNumber.
         */
        types::Result processResponse(uint32_t expectedNumber);

        /**
         * @brief Dopo il banner di avvio: raccoglie la riga "BOOT <causa>".
         */
        std::string readBootCause();
    };

} // namespace core
