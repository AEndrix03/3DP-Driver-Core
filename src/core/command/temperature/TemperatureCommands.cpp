// src/core/command/temperature/TemperatureCommands.cpp
#include "core/command/temperature/TemperatureCommands.hpp"
#include "core/DriverInterface.hpp"
#include "core/utils/FloatFormatter.hpp"
#include "core/printer/state/StateTracker.hpp"
#include "logger/Logger.hpp"

namespace core::command::temperature {
    TemperatureCommands::TemperatureCommands(DriverInterface *driver)
            : CommandCategoryInterface(driver) {
    }

    types::Result TemperatureCommands::setHotendTemperature(int temperature) {
        std::vector<std::string> params = {
                "S" + std::to_string(temperature)
        };
        auto result = sendCommand('T', 10, params);

        if (result.isSuccess()) {
            auto &stateTracker = state::StateTracker::getInstance();
            stateTracker.setHotendTargetTemp(temperature);
        }

        return result;
    }

    types::Result TemperatureCommands::setBedTemperature(int temperature) {
        std::vector<std::string> params = {
                "S" + std::to_string(temperature)
        };
        auto result = sendCommand('T', 20, params);

        if (result.isSuccess()) {
            auto &stateTracker = state::StateTracker::getInstance();
            stateTracker.setBedTargetTemp(temperature);
        }

        return result;
    }

    types::Result TemperatureCommands::getHotendTemperature() {
        // T11 command gets hotend temperature
        auto result = sendCommand('T', 11, {});

        if (result.isSuccess() && !result.body.empty()) {
            try {
                // Parse temperature from response body
                for (const auto &line: result.body) {
                    // Look for TEMP= pattern
                    size_t pos = line.find("TEMP=");
                    if (pos != std::string::npos) {
                        std::string tempStr = line.substr(pos + 5);
                        size_t endPos = tempStr.find(' ');
                        if (endPos != std::string::npos) {
                            tempStr = tempStr.substr(0, endPos);
                        }

                        double temp = std::stod(tempStr);
                        auto &stateTracker = state::StateTracker::getInstance();
                        stateTracker.updateHotendActualTemp(temp);

                        Logger::logInfo("[TemperatureCommands] Hotend temp: " + std::to_string(temp) + "°C");

                        // Add the parsed temperature to the result
                        result.body.clear();
                        result.body.push_back("T:" + std::to_string(temp));
                        break;
                    }
                }
            } catch (const std::exception &e) {
                Logger::logError("[TemperatureCommands] Failed to parse hotend temperature: " + std::string(e.what()));
            }
        }

        return result;
    }

    types::Result TemperatureCommands::getBedTemperature() {
        // T21 command gets bed temperature
        auto result = sendCommand('T', 21, {});

        if (result.isSuccess() && !result.body.empty()) {
            try {
                // Parse temperature from response body
                for (const auto &line: result.body) {
                    // Look for TEMP= pattern
                    size_t pos = line.find("TEMP=");
                    if (pos != std::string::npos) {
                        std::string tempStr = line.substr(pos + 5);
                        size_t endPos = tempStr.find(' ');
                        if (endPos != std::string::npos) {
                            tempStr = tempStr.substr(0, endPos);
                        }

                        double temp = std::stod(tempStr);
                        auto &stateTracker = state::StateTracker::getInstance();
                        stateTracker.updateBedActualTemp(temp);

                        Logger::logInfo("[TemperatureCommands] Bed temp: " + std::to_string(temp) + "°C");

                        // Add the parsed temperature to the result
                        result.body.clear();
                        result.body.push_back("T:" + std::to_string(temp));
                        break;
                    }
                }
            } catch (const std::exception &e) {
                Logger::logError("[TemperatureCommands] Failed to parse bed temperature: " + std::string(e.what()));
            }
        }

        return result;
    }
} // namespace core::command::temperature