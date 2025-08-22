//
// Created by redeg on 26/04/2025.
//

#include "core/command/fan/FanCommands.hpp"
#include "core/DriverInterface.hpp"

namespace core::command::fan {

    FanCommands::FanCommands(DriverInterface *driver)
            : CommandCategoryInterface(driver) {}

    types::Result FanCommands::setFanSpeed(int percent) {
        std::vector<std::string> params = {
                "S" + std::to_string(percent)
        };
        return sendCommand('F', 10, params);
    }

    types::Result FanCommands::turnOff() {
        return sendCommand('F', 0, {});
    }

} // namespace core::printer-command::fan
