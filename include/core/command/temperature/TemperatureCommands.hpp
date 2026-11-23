//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "core/command/CommandCategoryInterface.hpp"
#include "./../../types/Result.hpp"

namespace core::command::temperature {

/**
 * @brief Comandi relativi alla gestione delle temperature (categoria 'T').
 */
    class TemperatureCommands : public CommandCategoryInterface {
    public:
        explicit TemperatureCommands(DriverInterface *driver);

        types::Result setHotendTemperature(int temperature);

        types::Result setBedTemperature(int temperature);

        types::Result getHotendTemperature();

        types::Result getBedTemperature();

        /**
         * @brief Guadagni del PID dell'hotend (T40). I valori negativi restano invariati;
         * la risposta riporta sempre quelli in uso: "PID <kp> <ki> <kd> <uscita 0-255>".
         */
        types::Result setPid(double kp, double ki, double kd);
    };

} // namespace core::printer-command::temperature
