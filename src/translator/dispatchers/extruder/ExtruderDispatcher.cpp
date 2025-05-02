//
// Created by redeg on 01/05/2025.
//

#include "translator/dispatchers/extruder/ExtruderDispatcher.hpp"
#include <iostream>

namespace translator::gcode {

    ExtruderDispatcher::ExtruderDispatcher(std::shared_ptr<core::DriverInterface> driver)
            : driver_(std::move(driver)) {}

    bool ExtruderDispatcher::canHandle(const std::string &command) const {
        return command == "G10" || command == "G11";
    }

    bool ExtruderDispatcher::validate(const std::string &command, const std::map<std::string, double> &) const {
        return true;
    }

    void ExtruderDispatcher::handle(const std::string &command, const std::map<std::string, double> &params) {
        float length = params.count("L") ? params.at("L") : 5.0f;
        float feedrate = params.count("F") ? params.at("F") : 300.0f;

        if (command == "G10") {
            driver_->extruder()->retract(length, feedrate);
        } else if (command == "G11") {
            driver_->extruder()->extrude(length, feedrate);
        }
    }

} // namespace translator::gcode
