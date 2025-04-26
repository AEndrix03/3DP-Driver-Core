//
// Created by redeg on 26/04/2025.
//

#include "core/command/system/SystemCommands.hpp"
#include "core/DriverInterface.hpp"

namespace core::command::system {

    SystemCommands::SystemCommands(DriverInterface *driver)
            : CommandCategoryInterface(driver) {}

    types::Result SystemCommands::reset() {
        return sendCommand('S', 0, {});
    }

    types::Result SystemCommands::getSystemInfo() {
        return sendCommand('S', 10, {});
    }

} // namespace core::command::system
