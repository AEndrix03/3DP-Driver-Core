//
// Created by redeg on 26/04/2025.
//

#pragma once

#include <atomic>
#include "core/serial/SerialPort.hpp"
#include "CommandContext.hpp"
#include "types/Result.hpp"
#include "core/serial/handler/SerialProtocolHandler.hpp"
#include <memory>
#include <string>
#include <mutex>
#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>

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
            /// Silenzio sulla linea che chiude lo scambio dopo un errore nella finestra dei movimenti
            std::chrono::milliseconds windowQuiet{120};
        };

        CommandExecutor(std::shared_ptr<SerialPort> serial, std::shared_ptr<CommandContext> context);

        /**
         * @brief Invia un comando e attende la risposta terminale.
         * @param command Testo completo del comando (con N e checksum).
         * @param commandNumber Numero del comando.
         */
        types::Result sendCommandAndAwaitResponse(const std::string &command, uint32_t commandNumber);

        /// Movimenti binari che possono essere in volo senza risposta. Non oltre 8: il firmware
        /// riconosce come "gia' ricevuto" un frame rispedito solo entro 8 numeri dall'ultimo.
        static constexpr size_t WINDOW_FRAMES = 8;
        /// Margine lasciato libero nel buffer di ricezione del firmware.
        static constexpr size_t WINDOW_MARGIN = 8;

        /**
         * @brief Dimensione del buffer di ricezione del firmware (da M115): i byte dei movimenti
         * in volo non la superano mai. Finche' non e' nota vale quella minima (64).
         */
        void setReceiveBuffer(size_t bytes);

        /**
         * @brief Invia un movimento binario senza attendere la sua risposta.
         *
         * Al massimo WINDOW_FRAMES movimenti (e WINDOW_BYTES byte) restano in volo: se la
         * finestra e' piena attende prima la risposta del piu' vecchio. Un errore su un
         * movimento precedente viene restituito qui (o da drainMoves): a quel punto la
         * finestra e' vuota e il firmware scarta i movimenti rimasti in volo.
         *
         * Dopo un frame corrotto o una risposta persa i movimenti in volo vengono rispediti
         * dal primo che il firmware non ha accettato (i duplicati rispondono E03).
         *
         * @param wait true = attende anche la risposta di questo movimento
         */
        types::Result submitMove(const std::string &frame, uint32_t commandNumber, bool wait);

        /// Attende la risposta di tutti i movimenti in volo.
        types::Result drainMoves();

        /// true se ci sono movimenti in volo.
        bool hasPendingMoves();

        /// Ultimo "D" letto in una risposta ai movimenti.
        std::optional<uint32_t> lastWindowCompleted();

        void setTiming(const Timing &timing) { timing_ = timing; }

        const Timing &timing() const { return timing_; }

    private:
        std::shared_ptr<SerialPort> serial_;
        std::shared_ptr<CommandContext> context_;
        std::mutex serialMutex_;
        std::shared_ptr<SerialProtocolHandler> protocolHandler_;
        Timing timing_;

        struct PendingMove {
            uint32_t number;
            std::string frame;
        };
        std::deque<PendingMove> pending_; // protetta da serialMutex_
        size_t pendingBytes_ = 0;
        std::optional<uint32_t> windowCompleted_;

        /// Una riga terminale qualunque (non filtrata per numero), con le righe informative che la precedono.
        struct Reply {
            enum class Kind { Terminal, Silence, PortError, Reboot } kind = Kind::Silence;
            MessageCodeType code = MessageCodeType::OK;
            std::optional<uint32_t> number;
            std::optional<uint32_t> completed;
            std::vector<std::string> body;
            std::string text;
        };

        std::atomic<size_t> windowBytes_{64 - WINDOW_MARGIN};

        Reply nextReply(std::chrono::milliseconds silence);

        /// Risolve il movimento piu' vecchio in volo. Richiede serialMutex_.
        types::Result settleOldest();

        types::Result drainLocked();

        void popFront();

        /// Scarta i movimenti in volo e le loro risposte (dopo un rifiuto o un errore macchina).
        void abandonWindow();

        /// Dopo E01/E04: lascia arrivare le risposte rimaste, poi rispedisce da `expected`.
        bool resendFrom(uint32_t expected);

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
