//
// Created by redeg on 01/05/2025.
//

#include "translator/dispatchers/system/SystemDispatcher.hpp"
#include <iostream>

namespace translator::gcode {

    SystemDispatcher::SystemDispatcher(std::shared_ptr<core::DriverInterface> driver)
            : driver_(std::move(driver)) {}

    bool SystemDispatcher::canHandle(const std::string &command) const {
        return command == "G28" || command == "M24" || command == "M25" ||
               command == "M26" || command == "M105" || command == "M112" ||
               command == "M999";
    }

    bool SystemDispatcher::validate(const std::string &command, const std::map<std::string, double> &) const {
        return true;
    }

    void SystemDispatcher::handle(const std::string &command, const std::map<std::string, double> &) {
        if (command == "G28") {
            driver_->system()->homing();
        } else if (command == "M24") {
            driver_->system()->startPrint();
        } else if (command == "M25") {
            driver_->system()->pause();
        } else if (command == "M26") {
            driver_->system()->resume();
        } else if (command == "M105") {
            driver_->system()->printStatus();
        } else if (command == "M112") {
            driver_->system()->brutalReset();
        } else if (command == "M999") {
            driver_->system()->emergencyReset();
        } /*else if (command == "M0") {
            driver_->system()->stop();
        } else if (command == "M1") {
            driver_->system()->sleep();
        } else if (command == "M17") {
            driver_->system()->enableMotors();
        } else if (command == "M18") {
            driver_->system()->disableMotors();
        } else if (command == "M81") {
            driver_->system()->powerOff();
        }*/
    }

} // namespace translator::gcode