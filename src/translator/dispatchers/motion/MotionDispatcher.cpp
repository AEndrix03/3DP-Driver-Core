//
// Created by redeg on 01/05/2025.
//

#include "translator/dispatchers/motion/MotionDispatcher.hpp"
#include <iostream>

namespace translator::gcode {

    MotionDispatcher::MotionDispatcher(std::shared_ptr<core::DriverInterface> driver)
            : driver_(std::move(driver)) {}

    bool MotionDispatcher::canHandle(const std::string &command) const {
        return command == "G0" || command == "G1" || command == "G220" || command == "G999";
    }

    bool MotionDispatcher::validate(const std::string &command, const std::map<std::string, double> &params) const {
        if (command == "G0" || command == "G1") {
            return params.count("X") || params.count("Y") || params.count("Z");
        }
        if (command == "G220") {
            return params.count("X") || params.count("Y") || params.count("Z");
        }
        return true;
    }

    void MotionDispatcher::handle(const std::string &command, const std::map<std::string, double> &params) {
        if (command == "G0" || command == "G1") {
            double x = params.count("X") ? params.at("X") : -1;
            double y = params.count("Y") ? params.at("Y") : -1;
            double z = params.count("Z") ? params.at("Z") : -1;
            double f = params.count("F") ? params.at("F") : 1000;
            driver_->motion()->moveTo(x, y, z, f);
        } else if (command == "G220") {
            if (params.count("X")) driver_->motion()->diagnoseAxis("X", params.at("X"));
            if (params.count("Y")) driver_->motion()->diagnoseAxis("Y", params.at("Y"));
            if (params.count("Z")) driver_->motion()->diagnoseAxis("Z", params.at("Z"));
        } else if (command == "G999") {
            driver_->motion()->emergencyStop();
        }
    }

} // namespace translator::gcode

