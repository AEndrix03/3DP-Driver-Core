//
// Created by redeg on 26/04/2025.
//

#include "core/command/extruder/ExtruderCommands.hpp"
#include "core/DriverInterface.hpp"
#include "core/utils/FloatFormatter.hpp"

namespace core::command::extruder {
    ExtruderCommands::ExtruderCommands(DriverInterface *driver)
        : CommandCategoryInterface(driver) {
    }

    types::Result ExtruderCommands::extrude(float millimeters, float feedrate) {
        std::vector<std::string> params = {
            "E" + utils::formatFloat(millimeters, 4),
            "F" + utils::formatFloat(feedrate, 0)
        };
        return sendCommand('A', 10, params);
    }

    types::Result ExtruderCommands::retract(float millimeters, float feedrate) {
        std::vector<std::string> params = {
            "E" + utils::formatFloat(millimeters, 4),
            "F" + utils::formatFloat(feedrate, 0)
        };
        return sendCommand('A', 11, params);
    }
} // namespace core::command::extruder
