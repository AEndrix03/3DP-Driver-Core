#pragma once

#include "core/MoveFrame.hpp"
#include <cstdint>
#include <deque>
#include <vector>

namespace core::planning {

    /// Limiti della macchina, letti dal firmware (M115) cosi' restano scritti in un posto solo.
    struct MachineLimits {
        double stepsPerMm[4] = {80, 80, 400, 80};
        double maxFeed[4] = {5000.0 / 60, 5000.0 / 60, 150.0 / 60, 2000.0 / 60}; ///< mm/s
        double maxAcceleration[4] = {1000, 1000, 100, 1500};                     ///< mm/s^2
        double junctionDeviation = 0.02;                                         ///< mm
        double maxStepRate = 10000;                                              ///< step/s
        int queueBlocks = 15;                                                    ///< blocchi utilizzabili nel firmware
    };

    /// Un blocco pronto da inviare, con le velocita' pianificate (mm/s) per chi vuole verificarle.
    struct PlannedBlock {
        MoveFrame::PlannedMove move;
        double entry = 0.0;
        double exit = 0.0;
        double nominal = 0.0;
        double length = 0.0;
        double acceleration = 0.0; ///< mm/s^2 in accelerazione
        double deceleration = 0.0; ///< mm/s^2 in frenata (quella del piano, un po' piu' bassa)
        uint32_t tag = 0;          ///< etichetta data a push(): da quale movimento viene il blocco

        /// Durata prevista del blocco (s), con il profilo a trapezio che esegue il firmware.
        double seconds() const;
    };

    /**
     * @brief Pianificatore dei movimenti, lato host.
     *
     * Riceve segmenti in step e restituisce blocchi con il piano di velocita' gia' deciso:
     * velocita' nominale e accelerazione entro i limiti di ogni asse, velocita' alle
     * giunzioni ("junction deviation"), passate avanti e indietro, linear advance.
     *
     * Un segmento esce solo quando il suo piano non puo' piu' cambiare, cioe' quando sono
     * noti abbastanza segmenti dopo di lui (oppure con flush(), che chiude con un arresto).
     *
     * Il firmware garantisce comunque di potersi fermare entro i blocchi che ha in coda:
     * per questo il piano guarda avanti solo quanto la coda del firmware puo' contenere
     * (`horizon`), con le stesse formule intere del firmware. Cosi' il firmware esegue
     * esattamente quello che e' stato pianificato qui, ed e' su queste velocita' che si
     * calcola il linear advance.
     */
    class MotionPlanner {
    public:
        void configure(const MachineLimits &limits);

        const MachineLimits &limits() const { return limits_; }

        /**
         * @brief Linear advance: millimetri di filamento in piu' per ogni mm/s di velocita'
         * dell'estrusore (secondi). 0 = spento.
         */
        void setLinearAdvance(double k) { advanceK_ = k < 0.0 ? 0.0 : k; }

        double linearAdvance() const { return advanceK_; }

        /// Accelerazioni richieste dal G-code (M204), mm/s^2; 0 = quella massima della macchina.
        void setAccelerations(double print, double travel);

        /**
         * @brief Rallenta tutto il lavoro: le velocita' chieste dal G-code vengono moltiplicate
         * per `factor` (0.05 .. 1). Vale dai segmenti successivi.
         */
        void setSpeedFactor(double factor) { speedFactor_ = factor < 0.05 ? 0.05 : factor > 1.0 ? 1.0 : factor; }

        double speedFactor() const { return speedFactor_; }

        /**
         * @brief Tetto ai blocchi inviati al secondo (0 = nessuno).
         *
         * Il piano ne tiene conto: un segmento non viene percorso in meno del tempo che serve a
         * inviarlo, cosi' con un tetto basso la macchina va piu' piano in modo regolare invece di
         * svuotare la coda del firmware e procedere a strappi.
         */
        void setBlockRate(double perSecond) { blockRate_ = perSecond > 0.0 ? perSecond : 0.0; }

        double blockRate() const { return blockRate_; }

        /**
         * @brief Aggiunge un segmento.
         * @param steps step con segno X, Y, Z, E (ognuno entro MoveFrame::STEP_LIMIT)
         * @param feedMmMin velocita' richiesta lungo il percorso
         * @param out riceve i blocchi diventati definitivi
         * @param tag etichetta riportata sui blocchi che nascono da questo segmento
         */
        void push(const long long steps[4], double feedMmMin, std::vector<PlannedBlock> &out, uint32_t tag = 0);

        /// Chiude il piano con un arresto: escono tutti i segmenti in attesa.
        void flush(std::vector<PlannedBlock> &out);

        /// Scarta i segmenti in attesa (arresto, posizione non piu' nota).
        void reset();

        size_t pending() const { return pending_.size(); }

    private:
        struct Segment {
            uint32_t tag = 0;
            long long steps[4] = {0, 0, 0, 0};
            double unit[3] = {0, 0, 0};
            bool cartesian = false; ///< muove X, Y o Z
            bool print = false;     ///< muove X/Y/Z ed estrude
            double length = 0.0;    ///< mm lungo il percorso
            double nominal = 0.0;   ///< mm/s, multiplo di 1/64
            int acceleration = 0;   ///< mm/s^2
            int stepScale = 0;
            long long count = 0;    ///< step dell'asse dominante
            double gainSqr = 0.0;   ///< (mm/s)^2 guadagnati lungo tutto il segmento, come li calcola il firmware
            double gainPerMm = 0.0; ///< lo stesso, per mm di percorso (non saturato)
            double junction = 0.0;  ///< velocita' massima di ingresso data dalla giunzione
            double entry = 0.0;     ///< velocita' di ingresso definitiva
            double extrusion = 0.0; ///< mm di filamento per mm di percorso
            int cost = 1;           ///< blocchi del firmware che puo' diventare
        };

        MachineLimits limits_;
        std::deque<Segment> pending_;
        double advanceK_ = 0.0;
        double printAcceleration_ = 0.0;
        double travelAcceleration_ = 0.0;
        double speedFactor_ = 1.0;
        double blockRate_ = 0.0;

        double advance_ = 0.0;          ///< filamento attualmente "in anticipo" (mm)
        double advanceRemainder_ = 0.0; ///< frazione di step di anticipo non ancora inviata

        int horizon() const;

        bool describe(Segment &segment, double feedMmS) const;

        double junctionSpeed(const Segment &before, const Segment &after) const;

        void release(bool closing, std::vector<PlannedBlock> &out);

        void emit(const Segment &segment, double exit, std::vector<PlannedBlock> &out);

        void emitPart(const Segment &parent, const long long steps[4], double entry, double exit,
                      std::vector<PlannedBlock> &out);
    };

} // namespace core::planning
