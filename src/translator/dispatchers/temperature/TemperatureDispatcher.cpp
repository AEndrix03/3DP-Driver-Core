//
// Created by redeg on 01/05/2025.
//

#include "translator/dispatchers/temperature/TemperatureDispatcher.hpp"
#include <iostream>

#include "core/printer/state/StateTracker.hpp"

namespace translator::gcode {
    TemperatureDispatcher::TemperatureDispatcher(std::shared_ptr<core::DriverInterface> driver)
        : driver_(std::move(driver)) {
    }

    bool TemperatureDispatcher::canHandle(const std::string &command) const {
        return command == "M104" || command == "M140";
    }

    bool
    TemperatureDispatcher::validate(const std::string &command, const std::map<std::string, double> &params) const {
        return params.count("S");
    }

    void TemperatureDispatcher::handle(const std::string &command, const std::map<std::string, double> &params) {
        auto &stateTracker = core::state::StateTracker::getInstance();
        double temp = params.at("S");
        if (command == "M104") {
            auto result = driver_->temperature()->setHotendTemperature(temp);
            if (result.isSuccess()) {
                stateTracker.updateHotendTemp(temp); // Cache target temp
            }
        } else if (command == "M140") {
            auto result = driver_->temperature()->setBedTemperature(temp);
            if (result.isSuccess()) {
                stateTracker.updateBedTemp(temp); // Cache target temp
            }
        }

        stateTracker.updateLastCommand(command + " S" + std::to_string(static_cast<int>(temp)));
    }
} // namespace translator::gcode
