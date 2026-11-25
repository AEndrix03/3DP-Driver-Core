#pragma once

#include "core/serial/SerialPort.hpp"
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace local {

    /**
     * @brief Porta seriale "con la spia": passa tutto alla porta vera e tiene traccia di cio' che transita.
     *
     * Serve alla pagina per due cose che devono venire dalla macchina e non da una stima:
     *  - la traccia degli ultimi scambi (frame binari dei movimenti con la loro traduzione,
     *    comandi di testo, risposte);
     *  - quali movimenti il firmware ha davvero completato. Ogni risposta dice fino a quale
     *    comando e' arrivata l'esecuzione: i frame inviati e non ancora completati sono quelli
     *    che separano la posizione "inviata" dalla posizione reale della testina.
     *
     * Le risposte arrivano a scatti (durante un movimento lungo nessuna): tra l'una e l'altra
     * la posizione viene ricostruita dal profilo di velocita' che il frame stesso descrive
     * (ingresso, velocita' nominale, accelerazione), e riallineata a ogni risposta. Dentro un
     * movimento e' quindi una ricostruzione; ai suoi estremi e' un dato del firmware.
     */
    class WirePort : public core::SerialPort {
    public:
        explicit WirePort(std::shared_ptr<core::SerialPort> inner);

        void send(const std::string &data) override;

        void sendRaw(const std::string &bytes) override;

        std::string receiveLine() override;

        bool isOpen() const override { return inner_->isOpen(); }

        struct Live {
            long long outstanding[4] = {0, 0, 0, 0}; ///< step inviati e non ancora eseguiti (X, Y, Z, E)
            int queued = 0;          ///< movimenti inviati e non ancora completati
            double speed = 0.0;      ///< velocita' in questo istante lungo il percorso (mm/s), ricostruita; 0 da fermi
            double acceleration = 0.0;
            uint64_t framesTx = 0, acksRx = 0, textTx = 0, textRx = 0, bytesTx = 0, bytesRx = 0;
            double travelled = 0.0;  ///< mm percorsi da X, Y, Z da quando la porta e' aperta (eseguiti, non inviati)
            uint64_t resent = 0;     ///< frame ritrasmessi
            uint64_t errors = 0;     ///< risposte di errore o rifiuto
        };

        Live live();

        /// Quante volte piu' in fretta del reale scorre il tempo della macchina (solo simulazione).
        void setTimeScale(double scale);

        /// Righe di errore o rifiuto arrivate dalla scheda dall'ultima chiamata (le toglie dall'elenco).
        std::vector<std::string> takeErrors();

        /// Scambi con id maggiore di `after` (al massimo gli ultimi `limit`), in JSON: "last", "skipped", "wire".
        std::string json(uint64_t after, size_t limit);

    private:
        struct Entry {
            uint64_t id = 0;
            double seconds = 0.0;
            char direction = 'T'; ///< 'T' verso la scheda, 'R' dalla scheda
            const char *kind = "text";
            std::string name;     ///< cosa fa: MOVE, HOMING, GET_TEMPERATURE, ACK, OK...
            std::string hex;      ///< solo per i dati binari
            std::string text;
        };
        struct Pending {
            uint32_t number = 0;
            long long steps[4] = {0, 0, 0, 0};
            double speed = 0.0, acceleration = 0.0;
            double length = 0.0;   ///< mm lungo il percorso
            double entry = 0.0;    ///< velocita' di ingresso pianificata (mm/s)
            double exit = 0.0;     ///< velocita' di uscita: l'ingresso del movimento successivo, 0 se e' l'ultimo
            double start = 0.0;    ///< inizio previsto, sull'orologio della macchina (s)
            double duration = 0.0; ///< durata prevista (s)
            bool cartesian = false; ///< muove X, Y o Z (non solo l'estrusore)
        };

        std::shared_ptr<core::SerialPort> inner_;
        std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
        std::mutex mutex_;
        std::deque<Entry> entries_;
        std::deque<Pending> pending_;
        uint64_t nextId_ = 1;
        uint32_t lastNumber_ = 0;
        double modalSpeed_ = 0.0, modalAcceleration_ = 0.0;
        double timeScale_ = 1.0, machineTime_ = 0.0; ///< orologio della macchina: avanza di timeScale_ al secondo
        std::chrono::steady_clock::time_point machineStamp_ = std::chrono::steady_clock::now();

        double machineNow();
        Live counters_;
        double travelledDone_ = 0.0; ///< mm dei movimenti gia' confermati dal firmware
        std::vector<std::string> errorLines_;

        void record(char direction, const char *kind, const std::string &name, const std::string &hex,
                    const std::string &text);

        void completedUpTo(uint32_t number);
    };
}
