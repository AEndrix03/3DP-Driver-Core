//
// Created by redeg on 01/05/2025.
//

#include "translator/dispatchers/temperature/TemperatureDispatcher.hpp"
#include "logger/Logger.hpp"
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>

#include "core/printer/state/StateTracker.hpp"

namespace translator::gcode {
    namespace {
        constexpr double REACHED_TOLERANCE_C = 3.0;
        constexpr auto POLL_INTERVAL = std::chrono::seconds(1);
        constexpr auto HEATING_TIMEOUT = std::chrono::minutes(15);
    }

    TemperatureDispatcher::TemperatureDispatcher(std::shared_ptr<core::DriverInterface> driver)
        : driver_(std::move(driver)) {
    }

    bool TemperatureDispatcher::canHandle(const std::string &command) const {
        return command == "M104" || command == "M109" || command == "M105" ||
               command == "M140" || command == "M190";
    }

    bool
    TemperatureDispatcher::validate(const std::string &command, const std::map<std::string, double> &params) const {
        if (command == "M105") return true;
        return params.count("S") || params.count("R");
    }

    void TemperatureDispatcher::handle(const std::string &command, const std::map<std::string, double> &params) {
        auto &stateTracker = core::state::StateTracker::getInstance();

        if (command == "M105") {
            driver_->temperature()->getHotendTemperature();
            return;
        }

        const double temp = params.count("S") ? params.at("S") : params.at("R");

        if (command == "M140" || command == "M190") {
            driver_->temperature()->setBedTemperature(static_cast<int>(std::lround(temp)));
            stateTracker.setBedTargetTemp(temp);
            return;
        }

        const auto result = driver_->temperature()->setHotendTemperature(static_cast<int>(std::lround(temp)));
        if (!result.isSuccess()) {
            // Stampare a freddo non ha senso: il job deve fermarsi
            driver_->raiseFault("hotend temperature not set: " + result.message);
            throw std::runtime_error("hotend temperature not set: " + result.message);
        }
        stateTracker.setHotendTargetTemp(temp);

        if (command != "M109" || temp <= 0.0) return;

        // M109: attesa del raggiungimento. Il polling tiene anche vivo il
        // controllo "host presente" del firmware.
        Logger::logInfo("[TemperatureDispatcher] Waiting for hotend to reach " + std::to_string(temp) + " C");
        const uint64_t epoch = driver_->faultEpoch();
        const auto deadline = std::chrono::steady_clock::now() + HEATING_TIMEOUT;

        while (true) {
            const auto reading = driver_->temperature()->getHotendTemperature();
            if (!reading.isSuccess() || driver_->faultEpoch() != epoch) {
                throw std::runtime_error("heating interrupted: " + reading.message);
            }
            if (stateTracker.getCachedHotendTemp() >= temp - REACHED_TOLERANCE_C) {
                Logger::logInfo("[TemperatureDispatcher] Hotend temperature reached");
                return;
            }
            if (std::chrono::steady_clock::now() > deadline) {
                driver_->temperature()->setHotendTemperature(0);
                driver_->raiseFault("hotend heating timeout");
                throw std::runtime_error("hotend heating timeout");
            }
            std::this_thread::sleep_for(POLL_INTERVAL);
        }
    }
} // namespace translator::gcode
