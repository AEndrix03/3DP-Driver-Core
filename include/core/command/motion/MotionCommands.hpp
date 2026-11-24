//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "core/command/CommandCategoryInterface.hpp"
#include "./../../types/Result.hpp"
#include "core/types/Position.hpp"
#include "core/motion/MotionPlanner.hpp"
#include <optional>
#include <mutex>
#include <thread>
#include <cstdint>

namespace core::command::motion {

    /**
     * @brief Destinazione di un movimento rettilineo. Gli assi non valorizzati non vengono inviati.
     * `e` indica sempre una quantita' relativa di filamento (mm).
     */
    struct MoveTarget {
        std::optional<double> x;
        std::optional<double> y;
        std::optional<double> z;
        std::optional<double> e;
        std::optional<double> feedrate; // mm/min; assente = ultimo usato dal firmware
    };

/**
 * @brief Comandi relativi al movimento (categoria 'M').
 */
    class MotionCommands : public CommandCategoryInterface {
    public:
        explicit MotionCommands(DriverInterface *driver);

        types::Result emergencyStop();

        /**
         * @brief Spostamento relativo.
         *
         * I movimenti viaggiano in formato binario, in step e gia' pianificati: la conversione
         * da millimetri (con il resto frazionario dell'estrusore) e il piano di velocita'
         * (core::planning::MotionPlanner) si calcolano qui, su una copia della posizione del
         * firmware riletta ogni volta che puo' essere cambiata (vedi DriverInterface::positionEpoch).
         *
         * @param pipelined true = per il flusso di una stampa: il movimento entra nel piano e
         *        parte quando sono noti abbastanza movimenti successivi (o con flushPlanned).
         *        Un errore emerge alla chiamata successiva. false = pianificato da solo,
         *        con partenza e arrivo da fermo, e inviato subito.
         */
        types::Result moveRelative(const MoveTarget &target, bool pipelined = false);

        /// Spostamento a coordinate assolute (assi omessi invariati; `e` resta relativo).
        types::Result moveAbsolute(const MoveTarget &target, bool pipelined = false);

        /// M12: dichiara la posizione corrente (G92)
        types::Result setPosition(const MoveTarget &position);

        /// Compatibilita': spostamento relativo su tutti e tre gli assi
        types::Result moveTo(float x, float y, float z, float feedrate);

        /// Compatibilita': spostamento assoluto su tutti e tre gli assi
        types::Result goTo(float x, float y, float z, float feedrate);

        types::Result diagnoseAxis(const std::string &axis, float feedrate);

        /**
         * @brief Posizione reale in questo istante (non attende la fine dei movimenti).
         * @param referenced se non nullo: true se il firmware ha un riferimento valido
         *        (homing o G92 dall'accensione); false dopo un riavvio.
         */
        std::optional<position::Position> getPosition(bool *referenced = nullptr);

        /**
         * @brief Invia i movimenti ancora in attesa nel piano, chiudendolo con un arresto.
         * Ha effetto solo dal thread che li ha accodati: i comandi di altri thread (letture
         * periodiche di stato e temperatura) non devono far frenare una stampa in corso.
         */
        void flushPlanned();

        /// Linear advance (M900 K): secondi; 0 = spento.
        void setLinearAdvance(double k);

        double linearAdvance();

        /// Accelerazioni richieste dal G-code (M204), mm/s^2; 0 = massimo della macchina.
        void setAccelerations(double print, double travel);

    private:
        /// Copia della posizione del firmware, in step.
        struct StepMirror {
            bool valid = false;
            uint64_t epoch = 0;
            long long position[3] = {0, 0, 0};
            double stepsPerMm[4] = {0, 0, 0, 0};
            long long travelMax[3] = {0, 0, 0}; ///< corsa massima (step)
            int limitedAxes = 0;                ///< bit per asse: posizione riferita, la corsa e' limitata
            double eRemainder = 0.0;   ///< frazione di step di estrusione non ancora inviata
            double lastFeedrate = 0.0; ///< ultima velocita' richiesta (F e' modale)
            uint16_t sentSpeed = 0;    ///< velocita' nominale e accelerazione dell'ultimo frame:
            uint16_t sentAcceleration = 0; ///< il firmware le ricorda, si inviano solo se cambiano
        };

        std::recursive_mutex mirrorMutex_;
        StepMirror mirror_;
        planning::MotionPlanner planner_;
        std::thread::id plannerThread_;

        /// Invia i blocchi usciti dal piano. In caso di errore la copia della posizione non vale piu'.
        types::Result sendBlocks(const std::vector<planning::PlannedBlock> &blocks, bool pipelined);

        /// Rilegge dal firmware posizione e step/mm (M115).
        types::Result syncMirror();

        types::Result move(const MoveTarget &target, bool absolute, bool pipelined);
    };

} // namespace core::printer-command::motion
