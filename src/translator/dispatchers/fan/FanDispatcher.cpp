//
// Created by redeg on 01/05/2025.
//

#include "translator/dispatchers/fan/FanDispatcher.hpp"
#include <iostream>

namespace translator::gcode {

    FanDispatcher::FanDispatcher(std::shared_ptr<core::DriverInterface> driver)
            : driver_(std::move(driver)) {}

    bool FanDispatcher::canHandle(const std::string &command) const {
        return command == "M106" || command == "M107";
    }

    bool FanDispatcher::validate(const std::string &command, const std::map<std::string, double> &params) const {
        if (command == "M106") {
            return params.count("S");
        }
        return true;
    }

    void FanDispatcher::handle(const std::string &command, const std::map<std::string, double> &params) {
        if (command == "M106") {
            int speed = static_cast<int>(params.at("S"));
            driver_->fan()->setFanSpeed(speed);
        } else if (command == "M107") {
            driver_->fan()->setFanSpeed(0);
        }
    }


} // namespace translator::gcode
