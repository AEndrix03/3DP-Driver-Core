//
// Created by redeg on 01/05/2025.
//

#include "translator/dispatchers/temperature/TemperatureDispatcher.hpp"
#include <iostream>

namespace translator::gcode {

    TemperatureDispatcher::TemperatureDispatcher(std::shared_ptr<core::DriverInterface> driver)
            : driver_(std::move(driver)) {}

    bool TemperatureDispatcher::canHandle(const std::string &command) const {
        return command == "M104" || command == "M140";
    }

    bool
    TemperatureDispatcher::validate(const std::string &command, const std::map<std::string, double> &params) const {
        return params.count("S");
    }

    void TemperatureDispatcher::handle(const std::string &command, const std::map<std::string, double> &params) {
        double temp = params.at("S");
        if (command == "M104") {
            driver_->temperature()->setHotendTemperature(temp);
        } else if (command == "M140") {
            driver_->temperature()->setBedTemperature(temp);
        }
    }


} // namespace translator::gcode
