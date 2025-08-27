//
// Created by redeg on 01/05/2025.
//

#include "translator/dispatchers/extruder/ExtruderDispatcher.hpp"
#include <iostream>

#include "core/printer/state/StateTracker.hpp"

namespace translator::gcode {
    ExtruderDispatcher::ExtruderDispatcher(std::shared_ptr<core::DriverInterface> driver)
        : driver_(std::move(driver)) {
    }

    bool ExtruderDispatcher::canHandle(const std::string &command) const {
        return command == "G10" || command == "G11";
    }

    bool ExtruderDispatcher::validate(const std::string &command, const std::map<std::string, double> &) const {
        return true;
    }

    void ExtruderDispatcher::handle(const std::string &command, const std::map<std::string, double> &params) {
        auto &stateTracker = core::state::StateTracker::getInstance();
        float length = params.count("L") ? params.at("L") : 5.0f;
        float feedrate = params.count("F") ? params.at("F") : 300.0f;
        if (command == "G10") {
            // Retract
            auto result = driver_->extruder()->retract(length, feedrate);
            if (result.isSuccess()) {
                double currentE = stateTracker.getCurrentEPosition();
                stateTracker.updateEPosition(currentE - length); // Subtract for retract
            }
        } else if (command == "G11") {
            // Extrude/unretract
            auto result = driver_->extruder()->extrude(length, feedrate);
            if (result.isSuccess()) {
                double currentE = stateTracker.getCurrentEPosition();
                stateTracker.updateEPosition(currentE + length); // Add for extrude
            }
        }

        stateTracker.updateLastCommand(command + " L" + std::to_string(length));
        stateTracker.updateFeedRate(feedrate);
    }
} // namespace translator::gcode
