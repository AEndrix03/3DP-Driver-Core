//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "core/command/CommandCategoryInterface.hpp"
#include "./../../types/Result.hpp"

namespace core::command::endstop {

/**
 * @brief Comandi relativi agli endstop (categoria 'E').
 */
    class EndstopCommands : public CommandCategoryInterface {
    public:
        explicit EndstopCommands(DriverInterface *driver);

        types::Result readEndstopStatus();
    };

} // namespace core::printer-command::endstop
