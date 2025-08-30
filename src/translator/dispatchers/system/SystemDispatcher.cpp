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
        bool canHandle = command == "G28" || command == "M24" || command == "M25" ||
                         command == "M26" || command == "M105" || command == "M112" ||
                         command == "M999";

        if (canHandle) {
            Logger::logInfo("[SystemDispatcher] Can handle command: " + command);
        }

        return canHandle;
    }

    bool SystemDispatcher::validate(const std::string &command, const std::map<std::string, double> &) const {
        Logger::logInfo("[SystemDispatcher] Validating command: " + command);
        return true;
    }

    void SystemDispatcher::handle(const std::string &command, const std::map<std::string, double> &) {
        Logger::logInfo("[SystemDispatcher] Handling command: " + command);

        if (command == "G28") {
            Logger::logInfo("[SystemDispatcher] Executing homing");
            driver_->system()->homing();
        } else if (command == "M24") {
            Logger::logInfo("[SystemDispatcher] Executing start print");
            driver_->system()->startPrint();
        } else if (command == "M25") {
            Logger::logInfo("[SystemDispatcher] Executing pause");
            driver_->system()->pause();
        } else if (command == "M26") {
            Logger::logInfo("[SystemDispatcher] Executing resume");
            driver_->system()->resume();
        } else if (command == "M105") {
            Logger::logInfo("[SystemDispatcher] Executing print status");
            driver_->system()->printStatus();
        } else if (command == "M112") {
            Logger::logInfo("[SystemDispatcher] Executing brutal reset");
            driver_->system()->brutalReset();
        } else if (command == "M999") {
            Logger::logInfo("[SystemDispatcher] Executing emergency reset");
            driver_->system()->emergencyReset();
        }

        Logger::logInfo("[SystemDispatcher] Command handled successfully: " + command);
    }

} // namespace translator::gcode