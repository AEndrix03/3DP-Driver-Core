//
// Created by redeg on 26/04/2025.
//

#pragma once

#include <string>

#include "core/command/CommandCategoryInterface.hpp"
#include "./../../types/Result.hpp"

namespace core::command::system {

/**
 * @brief Comandi relativi al sistema (categoria 'S').
 */
    class SystemCommands : public CommandCategoryInterface {
    public:
        explicit SystemCommands(DriverInterface *driver);

        types::Result homing();

        types::Result startPrint();

        types::Result pause();

        types::Result resume();

        types::Result emergencyReset();

        types::Result brutalReset();

        types::Result printStatus();

        /// S0 con assi: homing dei soli assi indicati (es. "XY")
        types::Result homeAxes(const std::string &axes);

        /// S6: azzera lo stato di errore del firmware
        types::Result clearError();

        /// S7: causa ed eta' dell'errore attivo
        types::Result errorStatus();

        /// S11: uptime, causa ultimo reset, contatori errori seriale, RAM libera
        types::Result diagnostics();

        /// S12: fine stampa (Printing/Paused -> Idle)
        types::Result endPrint();
    };

} // namespace core::printer-command::system
