//
// Created by redeg on 01/05/2025.
//

#include "translator/dispatchers/endstop/EndstopDispatcher.hpp"
#include <iostream>

namespace translator::gcode {

    EndstopDispatcher::EndstopDispatcher(std::shared_ptr<core::DriverInterface> driver)
            : driver_(std::move(driver)) {}

    bool EndstopDispatcher::canHandle(const std::string &command) const {
        return command == "M119";
    }

    bool EndstopDispatcher::validate(const std::string &command, const std::map<std::string, double> &) const {
        return true;
    }

    void EndstopDispatcher::handle(const std::string &command, const std::map<std::string, double> &) {
        driver_->endstop()->readEndstopStatus();
    }

} // namespace translator::gcode