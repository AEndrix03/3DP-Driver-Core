//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "core/command/CommandCategoryInterface.hpp"
#include "./../../types/Result.hpp"

namespace core::command::history {

/**
 * @brief Comandi relativi alla cronologia dei comandi (categoria 'H').
 */
    class HistoryCommands : public CommandCategoryInterface {
    public:
        explicit HistoryCommands(DriverInterface *driver);

        types::Result clearHistory();
    };

} // namespace core::printer-command::history
