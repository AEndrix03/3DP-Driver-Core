//
// Created by redeg on 01/05/2025.
//

#include "translator/dispatchers/motion/MotionDispatcher.hpp"
#include <iostream>
#include <cmath>

namespace translator::gcode {

    MotionDispatcher::MotionDispatcher(std::shared_ptr<core::DriverInterface> driver)
            : driver_(std::move(driver)) {}

    bool MotionDispatcher::canHandle(const std::string &command) const {
        return command == "G0" || command == "G1" || command == "G220" || command == "G999" ||
               command == "G2" || command == "G3" || command == "G5" ||
               command == "G92" || command == "M114";
    }

    bool MotionDispatcher::validate(const std::string &command, const std::map<std::string, double> &params) const {
        if (command == "G0" || command == "G1") {
            return params.count("X") || params.count("Y") || params.count("Z");
        }
        if (command == "G220") {
            return params.count("X") || params.count("Y") || params.count("Z");
        }
        if (command == "G2" || command == "G3") {
            return params.count("X") && params.count("Y") && params.count("I") && params.count("J");
        }
        if (command == "G5") {
            return params.count("X") && params.count("Y") &&
                   params.count("I") && params.count("J") &&
                   params.count("P") && params.count("Q");
        }
        return true;
    }

    void MotionDispatcher::handle(const std::string &command, const std::map<std::string, double> &params) {
        if (command == "G0" || command == "G1") {
            double x = params.count("X") ? params.at("X") : -1;
            double y = params.count("Y") ? params.at("Y") : -1;
            double z = params.count("Z") ? params.at("Z") : -1;
            double f = params.count("F") ? params.at("F") : 1000;
            driver_->motion()->moveTo(x, y, z, f);
        } else if (command == "G220") {
            if (params.count("X")) driver_->motion()->diagnoseAxis("X", params.at("X"));
            if (params.count("Y")) driver_->motion()->diagnoseAxis("Y", params.at("Y"));
            if (params.count("Z")) driver_->motion()->diagnoseAxis("Z", params.at("Z"));
        } else if (command == "G999") {
            driver_->motion()->emergencyStop();
        } else if (command == "G2" || command == "G3") {
            double x = params.at("X");
            double y = params.at("Y");
            double i = params.at("I");
            double j = params.at("J");
            double f = params.count("F") ? params.at("F") : 1000;

            constexpr int segments = 20;
            position::Position pos = driver_->motion()->getPosition().value();

            double cx = pos.x + i;
            double cy = pos.y + j;
            double radius = std::hypot(pos.x - cx, pos.y - cy);

            double startAngle = std::atan2(pos.y - cy, pos.x - cx);
            double endAngle = std::atan2(y - cy, x - cx);

            double deltaAngle = endAngle - startAngle;
            if (command == "G2" && deltaAngle > 0) deltaAngle -= 2 * M_PI;
            if (command == "G3" && deltaAngle < 0) deltaAngle += 2 * M_PI;

            for (int i = 1; i <= segments; ++i) {
                double angle = startAngle + deltaAngle * i / segments;
                double px = cx + radius * std::cos(angle);
                double py = cy + radius * std::sin(angle);
                driver_->motion()->goTo(static_cast<int32_t>(px), static_cast<int32_t>(py), -1, f);
            }
        } else if (command == "G5") {
            double x = params.at("X");
            double y = params.at("Y");
            double i = params.at("I");
            double j = params.at("J");
            double p = params.at("P");
            double q = params.at("Q");
            double f = params.count("F") ? params.at("F") : 1000;

            constexpr int segments = 20;
            position::Position pos = driver_->motion()->getPosition().value();

            double x0 = pos.x;
            double y0 = pos.y;

            for (int s = 1; s <= segments; ++s) {
                double t = static_cast<double>(s) / segments;
                double u = 1.0 - t;

                double px = u * u * u * x0 + 3 * u * u * t * i + 3 * u * t * t * p + t * t * t * x;
                double py = u * u * u * y0 + 3 * u * u * t * j + 3 * u * t * t * q + t * t * t * y;

                driver_->motion()->goTo(static_cast<int32_t>(px), static_cast<int32_t>(py), -1, f);
            }
        } else if (command == "G92") {
            int32_t x = 0, y = 0, z = 0;
            if (params.count("X")) x = static_cast<int32_t>(params.at("X"));
            if (params.count("Y")) y = static_cast<int32_t>(params.at("Y"));
            if (params.count("Z")) z = static_cast<int32_t>(params.at("Z"));
            driver_->motion()->setPosition(x, y, z);
        } else if (command == "M114") {
            driver_->motion()->getPosition(); // Assume log interno dal firmware
        }
    }

} // namespace translator::gcode
