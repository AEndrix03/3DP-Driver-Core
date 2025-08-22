//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "core/command/CommandCategoryInterface.hpp"
#include "./../../types/Result.hpp"

namespace core::command::extruder {

/**
 * @brief Comandi relativi all'estrusore (categoria 'A').
 */
    class ExtruderCommands : public CommandCategoryInterface {
    public:
        explicit ExtruderCommands(DriverInterface *driver);

        types::Result extrude(float millimeters, float feedrate);

        types::Result retract(float millimeters, float feedrate);
    };

} // namespace core::printer-command::extruder
