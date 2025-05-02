//
// Created by redeg on 26/04/2025.
//

#include <regex>
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

    types::Result MotionCommands::goTo(float x, float y, float z, float feedrate) {
        std::vector<std::string> params = {
                "X" + std::to_string(x),
                "Y" + std::to_string(y),
                "Z" + std::to_string(z),
                "F" + std::to_string(feedrate)
        };
        return sendCommand('M', 11, params);
    }

    std::optional<position::Position> MotionCommands::getPosition() {
        auto result = sendCommand('M', 114, {});
        if (!result.isSuccess()) return std::nullopt;

        position::Position pos;
        std::regex rxX(R"(X=([-]?[0-9]*\.?[0-9]+))");
        std::regex rxY(R"(Y=([-]?[0-9]*\.?[0-9]+))");
        std::regex rxZ(R"(Z=([-]?[0-9]*\.?[0-9]+))");
        std::smatch match;

        for (const auto &line: result.body) {
            if (std::regex_search(line, match, rxX)) {
                pos.x = std::stod(match[1]);  // ora in mm
            }
            if (std::regex_search(line, match, rxY)) {
                pos.y = std::stod(match[1]);
            }
            if (std::regex_search(line, match, rxZ)) {
                pos.z = std::stod(match[1]);
            }
        }

        return pos;
    }


} // namespace core::command::motion
