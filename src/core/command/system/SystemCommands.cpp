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

    types::Result SystemCommands::homeAxes(const std::string &axes) {
        std::vector<std::string> params;
        for (char axis: axes) params.emplace_back(1, axis);
        return sendCommand('S', 0, params);
    }

    types::Result SystemCommands::clearError() {
        return sendCommand('S', 6, {});
    }

    types::Result SystemCommands::errorStatus() {
        return sendCommand('S', 7, {});
    }

    types::Result SystemCommands::diagnostics() {
        return sendCommand('S', 11, {});
    }

    types::Result SystemCommands::endPrint() {
        return sendCommand('S', 12, {});
    }

} // namespace core::printer-command::system
