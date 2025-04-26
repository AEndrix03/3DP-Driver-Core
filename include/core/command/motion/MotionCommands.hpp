//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "core/command/CommandCategoryInterface.hpp"
#include "./../../types/Result.hpp"

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
    };

} // namespace core::command::motion
