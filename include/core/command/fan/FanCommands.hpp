//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "core/command/CommandCategoryInterface.hpp"
#include "./../../types/Result.hpp"

namespace core::command::fan {

/**
 * @brief Comandi relativi alla gestione delle ventole (categoria 'F').
 */
    class FanCommands : public CommandCategoryInterface {
    public:
        explicit FanCommands(DriverInterface *driver);

        types::Result setFanSpeed(int percent);

        types::Result turnOff();
    };

} // namespace core::printer-command::fan
