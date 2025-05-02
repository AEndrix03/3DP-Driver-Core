//
// Created by redeg on 26/04/2025.
//

#include "core/command/motion/MotionCommands.hpp"
#include "core/DriverInterface.hpp"

namespace core::command::motion {

    MotionCommands::MotionCommands(DriverInterface *driver)
            : CommandCategoryInterface(driver) {}

    types::Result MotionCommands::emergencyStop() {
        return sendCommand('M', 0, {});
    }

    types::Result MotionCommands::moveTo(float x, float y, float z, float feedrate) {
        std::vector<std::string> params = {
                "X" + std::to_string(x),
                "Y" + std::to_string(y),
                "Z" + std::to_string(z),
                "F" + std::to_string(feedrate)
        };
        return sendCommand('M', 10, params);
    }

    types::Result MotionCommands::diagnoseAxis(const std::string &axis, float feedrate) {
        std::vector<std::string> params = {
                axis,
                "F" + std::to_string(feedrate)
        };
        return sendCommand('M', 99, params);
    }

    types::Result MotionCommands::goTo(int x, int y, int z, float feedrate) {
        std::vector<std::string> params = {
                "X" + std::to_string(x),
                "Y" + std::to_string(y),
                "Z" + std::to_string(z),
                "F" + std::to_string(feedrate)
        };
        return sendCommand('M', 11, params);
    }

    types::Result MotionCommands::setPosition(int x, int y, int z) {
        std::vector<std::string> params = {
                "X" + std::to_string(x),
                "Y" + std::to_string(y),
                "Z" + std::to_string(z)
        };
        return sendCommand('M', 12, params);
    }

    types::Result MotionCommands::zeroPosition() {
        return sendCommand('M', 13, {});
    }

    types::Result MotionCommands::getPosition() {
        return sendCommand('M', 114, {});
    }


} // namespace core::command::motion
