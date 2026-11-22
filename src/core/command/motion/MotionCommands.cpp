#include "core/command/motion/MotionCommands.hpp"
#include "core/DriverInterface.hpp"
#include "core/utils/FloatFormatter.hpp"
#include <sstream>

namespace core::command::motion {
    namespace {
        // X/Y/Z a 3 decimali (risoluzione meccanica 0.0125 mm), E a 4: il firmware
        // riporta il resto frazionario, quindi nessuna deriva di estrusione.
        std::vector<std::string> buildParams(const MoveTarget &t) {
            std::vector<std::string> params;
            if (t.x) params.push_back("X" + utils::formatFloat(*t.x, 3));
            if (t.y) params.push_back("Y" + utils::formatFloat(*t.y, 3));
            if (t.z) params.push_back("Z" + utils::formatFloat(*t.z, 3));
            if (t.e) params.push_back("E" + utils::formatFloat(*t.e, 4));
            if (t.feedrate) params.push_back("F" + utils::formatFloat(*t.feedrate, 0));
            return params;
        }
    }

    MotionCommands::MotionCommands(DriverInterface *driver)
        : CommandCategoryInterface(driver) {
    }

    types::Result MotionCommands::emergencyStop() {
        return sendCommand('M', 0, {});
    }

    types::Result MotionCommands::moveRelative(const MoveTarget &target) {
        return sendCommand('M', 10, buildParams(target));
    }

    types::Result MotionCommands::moveAbsolute(const MoveTarget &target) {
        return sendCommand('M', 11, buildParams(target));
    }

    types::Result MotionCommands::setPosition(const MoveTarget &position) {
        MoveTarget p = position;
        p.e.reset();
        p.feedrate.reset();
        return sendCommand('M', 12, buildParams(p));
    }

    types::Result MotionCommands::moveTo(float x, float y, float z, float feedrate) {
        return moveRelative({x, y, z, std::nullopt, feedrate});
    }

    types::Result MotionCommands::goTo(float x, float y, float z, float feedrate) {
        return moveAbsolute({x, y, z, std::nullopt, feedrate});
    }

    types::Result MotionCommands::diagnoseAxis(const std::string &axis, float feedrate) {
        std::vector<std::string> params = {
            axis,
            "F" + utils::formatFloat(feedrate, 0)
        };
        return sendCommand('M', 99, params);
    }

    std::optional<position::Position> MotionCommands::getPosition(bool *referenced) {
        auto result = sendCommand('M', 114, {});
        if (!result.isSuccess()) return std::nullopt;

        // Risposta del firmware: "CRT POS <x> <y> <z> <riferita 0/1>"
        for (const auto &line: result.body) {
            std::istringstream iss(line);
            std::string token;
            iss >> token;
            if (token == "CRT") iss >> token;
            if (token != "POS") continue;

            position::Position pos{};
            if (iss >> pos.x >> pos.y >> pos.z) {
                int flag = 0;
                if (referenced) *referenced = (iss >> flag) && flag != 0;
                return pos;
            }
        }

        return std::nullopt;
    }
} // namespace core::command::motion
