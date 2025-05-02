//
// Created by redeg on 01/05/2025.
//

#include "translator/dispatchers/history/HistoryDispatcher.hpp"
#include <iostream>

namespace translator::gcode {

    HistoryDispatcher::HistoryDispatcher(std::shared_ptr<core::DriverInterface> driver)
            : driver_(std::move(driver)) {}

    bool HistoryDispatcher::canHandle(const std::string &command) const {
        return command == "M702";
    }

    bool HistoryDispatcher::validate(const std::string &command, const std::map<std::string, double> &) const {
        return true;
    }

    void HistoryDispatcher::handle(const std::string &command, const std::map<std::string, double> &) {
        if (command == "M702") {
            driver_->history()->clearHistory();
        }
    }


} // namespace translator::gcode
