//
// Created by redeg on 01/05/2025.
//

#include "translator/dispatchers/system/SystemDispatcher.hpp"
#include "logger/Logger.hpp"
#include <iostream>

namespace translator::gcode {

    SystemDispatcher::SystemDispatcher(std::shared_ptr<core::DriverInterface> driver)
            : driver_(std::move(driver)) {}

    bool SystemDispatcher::canHandle(const std::string &command) const {
        // G28 e' gestito da MotionDispatcher (deve invalidare la posizione nota),
        // M105 da TemperatureDispatcher.
        return command == "M24" || command == "M25" || command == "M26" ||
               command == "M112" || command == "M999";
    }

    bool SystemDispatcher::validate(const std::string &, const std::map<std::string, double> &) const {
        return true;
    }

    void SystemDispatcher::handle(const std::string &command, const std::map<std::string, double> &) {
        Logger::logInfo("[SystemDispatcher] Handling command: " + command);

        if (command == "M24") {
            driver_->system()->startPrint();
        } else if (command == "M25") {
            driver_->system()->pause();
        } else if (command == "M26") {
            driver_->system()->resume();
        } else if (command == "M112") {
            driver_->system()->brutalReset();
        } else if (command == "M999") {
            driver_->system()->emergencyReset();
        }
    }

} // namespace translator::gcode