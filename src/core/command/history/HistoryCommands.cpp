//
// Created by redeg on 26/04/2025.
//

#include "core/command/history/HistoryCommands.hpp"
#include "core/DriverInterface.hpp"

namespace core::command::history {

    HistoryCommands::HistoryCommands(DriverInterface *driver)
            : CommandCategoryInterface(driver) {}

    types::Result HistoryCommands::clearHistory() {
        return sendCommand('H', 0, {});
    }

} // namespace core::command::history
