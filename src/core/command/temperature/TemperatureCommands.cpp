// src/core/command/temperature/TemperatureCommands.cpp
#include "core/command/temperature/TemperatureCommands.hpp"
#include "core/DriverInterface.hpp"
#include "core/utils/FloatFormatter.hpp"
#include "core/printer/state/StateTracker.hpp"
#include "logger/Logger.hpp"
#include <sstream>

namespace core::command::temperature {
    TemperatureCommands::TemperatureCommands(DriverInterface *driver)
            : CommandCategoryInterface(driver) {
    }

    namespace {
        /// Estrae "<attuale> <target>" dalla riga "CRT TMP <attuale> <target>"
        bool parseTemperature(const types::Result &result, double &current, double &target) {
            for (const auto &line: result.body) {
                std::istringstream iss(line);
                std::string token;
                iss >> token;
                if (token == "CRT") iss >> token;
                if (token != "TMP") continue;
                if (iss >> current >> target) return true;
            }
            return false;
        }
    }

    types::Result TemperatureCommands::setHotendTemperature(int temperature) {
        // Firmware: T10 S<gradi>. La regolazione avviene sul firmware.
        auto result = sendCommand('T', 10, {"S" + std::to_string(temperature)});

        if (result.isSuccess()) {
            state::StateTracker::getInstance().setHotendTargetTemp(temperature);
        }

        return result;
    }

    types::Result TemperatureCommands::setBedTemperature(int temperature) {
        // Il firmware non gestisce un piano riscaldato: nessun comando viene inviato.
        if (temperature > 0) {
            Logger::logWarning("[TemperatureCommands] Heated bed not supported by firmware - M140/M190 ignored");
        }
        return types::Result::success("Heated bed not supported");
    }

    types::Result TemperatureCommands::setPid(double kp, double ki, double kd) {
        std::vector<std::string> params;
        if (kp >= 0.0) params.push_back("P" + utils::formatFloat(kp, 2));
        if (ki >= 0.0) params.push_back("I" + utils::formatFloat(ki, 3));
        if (kd >= 0.0) params.push_back("D" + utils::formatFloat(kd, 1));
        return sendCommand('T', 40, params);
    }

    types::Result TemperatureCommands::getHotendTemperature() {
        // Firmware: T20 -> "CRT TMP <attuale> <target>"
        auto result = sendCommand('T', 20, {});

        double current = 0.0, target = 0.0;
        if (result.isSuccess() && parseTemperature(result, current, target)) {
            state::StateTracker::getInstance().updateHotendActualTemp(current);
            result.body.clear();
            result.body.push_back("T:" + std::to_string(current));
            result.body.push_back("TARGET:" + std::to_string(target));
        }

        return result;
    }

    types::Result TemperatureCommands::getBedTemperature() {
        return types::Result::error("Heated bed not supported");
    }
} // namespace core::command::temperature
