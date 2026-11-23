//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "core/command/CommandCategoryInterface.hpp"
#include "./../../types/Result.hpp"
#include "core/types/Position.hpp"
#include <optional>
#include <mutex>
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
         * I movimenti viaggiano in formato binario, in step: la conversione da millimetri
         * (con il resto frazionario dell'estrusore) avviene qui, su una copia della posizione
         * del firmware riletta ogni volta che puo' essere cambiata (vedi DriverInterface::positionEpoch).
         *
         * @param pipelined true = non attende la risposta: per il flusso di una stampa.
         *        Un errore emerge alla chiamata successiva.
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

    private:
        /// Copia della posizione del firmware, in step.
        struct StepMirror {
            bool valid = false;
            uint64_t epoch = 0;
            long long position[3] = {0, 0, 0};
            double stepsPerMm[4] = {0, 0, 0, 0};
            double eRemainder = 0.0;   ///< frazione di step di estrusione non ancora inviata
            double lastFeedrate = 0.0; ///< ultima velocita' inviata (il firmware la ricorda)
        };

        std::mutex mirrorMutex_;
        StepMirror mirror_;

        /// Rilegge dal firmware posizione e step/mm (M115).
        types::Result syncMirror();

        types::Result move(const MoveTarget &target, bool absolute, bool pipelined);
    };

} // namespace core::printer-command::motion
