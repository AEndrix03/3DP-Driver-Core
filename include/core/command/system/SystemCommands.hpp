//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "core/command/CommandCategoryInterface.hpp"
#include "./../../types/Result.hpp"

namespace core::command::system {

/**
 * @brief Comandi relativi al sistema (categoria 'S').
 */
    class SystemCommands : public CommandCategoryInterface {
    public:
        explicit SystemCommands(DriverInterface *driver);

        types::Result reset();

        types::Result getSystemInfo();
    };

} // namespace core::command::system
