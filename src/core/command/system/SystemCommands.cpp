//
// Created by redeg on 26/04/2025.
//

#include "core/command/system/SystemCommands.hpp"
#include "core/DriverInterface.hpp"

namespace core::command::system {

    SystemCommands::SystemCommands(DriverInterface *driver)
            : CommandCategoryInterface(driver) {}

    types::Result SystemCommands::homing() {
        return sendCommand('S', 0, {});
    }

    types::Result SystemCommands::startPrint() {
        return sendCommand('S', 1, {});
    }

    types::Result SystemCommands::pause() {
        return sendCommand('S', 2, {});
    }

    types::Result SystemCommands::resume() {
        return sendCommand('S', 3, {});
    }

    types::Result SystemCommands::emergencyReset() {
        return sendCommand('S', 4, {});
    }

    types::Result SystemCommands::brutalReset() {
        return sendCommand('S', 5, {});
    }

    types::Result SystemCommands::printStatus() {
        return sendCommand('S', 10, {});
    }

} // namespace core::printer-command::system
