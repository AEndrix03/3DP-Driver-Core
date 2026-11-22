//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "core/command/CommandCategoryInterface.hpp"
#include "./../../types/Result.hpp"
#include "core/types/Position.hpp"
#include <optional>

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

        /// M10: spostamento relativo
        types::Result moveRelative(const MoveTarget &target);

        /// M11: spostamento a coordinate assolute (assi omessi invariati)
        types::Result moveAbsolute(const MoveTarget &target);

        /// M12: dichiara la posizione corrente (G92)
        types::Result setPosition(const MoveTarget &position);

        /// Compatibilita': spostamento relativo su tutti e tre gli assi
        types::Result moveTo(float x, float y, float z, float feedrate);

        /// Compatibilita': spostamento assoluto su tutti e tre gli assi
        types::Result goTo(float x, float y, float z, float feedrate);

        types::Result diagnoseAxis(const std::string &axis, float feedrate);

        std::optional<position::Position> getPosition();

    };

} // namespace core::printer-command::motion
