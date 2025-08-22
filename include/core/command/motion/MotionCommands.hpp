//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "core/command/CommandCategoryInterface.hpp"
#include "./../../types/Result.hpp"
#include "core/types/Position.hpp"

namespace core::command::motion {

/**
 * @brief Comandi relativi al movimento (categoria 'M').
 */
    class MotionCommands : public CommandCategoryInterface {
    public:
        explicit MotionCommands(DriverInterface *driver);

        types::Result emergencyStop();

        types::Result moveTo(float x, float y, float z, float feedrate);

        types::Result diagnoseAxis(const std::string &axis, float feedrate);

        types::Result goTo(float x, float y, float z, float feedrate);

        std::optional<position::Position> getPosition();

    };

} // namespace core::printer-command::motion
