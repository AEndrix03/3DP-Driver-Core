//
// Created by redeg on 26/04/2025.
//

#include "core/command/temperature/TemperatureCommands.hpp"
#include "core/DriverInterface.hpp"

namespace core::command::temperature {

    TemperatureCommands::TemperatureCommands(DriverInterface *driver)
            : CommandCategoryInterface(driver) {}

    types::Result TemperatureCommands::setHotendTemperature(int temperature) {
        std::vector<std::string> params = {
                "S" + std::to_string(temperature)
        };
        return sendCommand('T', 10, params);
    }

    types::Result TemperatureCommands::setBedTemperature(int temperature) {
        std::vector<std::string> params = {
                "S" + std::to_string(temperature)
        };
        return sendCommand('T', 20, params);
    }

    types::Result TemperatureCommands::getHotendTemperature() {
        return sendCommand('T', 11, {});
    }

    types::Result TemperatureCommands::getBedTemperature() {
        return sendCommand('T', 21, {});
    }

} // namespace core::printer-command::temperature
