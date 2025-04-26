//
// Created by redeg on 26/04/2025.
//

#include "core/command/extruder/ExtruderCommands.hpp"
#include "core/DriverInterface.hpp"

namespace core::command::extruder {

    ExtruderCommands::ExtruderCommands(DriverInterface *driver)
            : CommandCategoryInterface(driver) {}

    types::Result ExtruderCommands::extrude(float millimeters, float feedrate) {
        std::vector<std::string> params = {
                "E" + std::to_string(millimeters),
                "F" + std::to_string(feedrate)
        };
        return sendCommand('A', 10, params);
    }

    types::Result ExtruderCommands::retract(float millimeters, float feedrate) {
        std::vector<std::string> params = {
                "E" + std::to_string(millimeters),
                "F" + std::to_string(feedrate)
        };
        return sendCommand('A', 20, params);
    }

} // namespace core::command::extruder
