//
// Created by redeg on 26/04/2025.
//

#include "core/command/endstop/EndstopCommands.hpp"
#include "core/DriverInterface.hpp"

namespace core::command::endstop {

    EndstopCommands::EndstopCommands(DriverInterface *driver)
            : CommandCategoryInterface(driver) {}

    types::Result EndstopCommands::readEndstopStatus() {
        return sendCommand('E', 10, {});
    }

} // namespace core::command::endstop
