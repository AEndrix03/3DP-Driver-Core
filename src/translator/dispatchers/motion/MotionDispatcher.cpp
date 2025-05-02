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
               command == "G2" || command == "G3" || command == "G5";
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

            // Calcolo dell'arco simulato
            constexpr int segments = 20;
            double cx = -i;
            double cy = -j;
            double radius = std::sqrt(cx * cx + cy * cy);

            double startAngle = std::atan2(-cy, -cx);
            double endAngle = std::atan2(y - cy, x - cx);

            // correzione direzione G2 (orario) o G3 (antiorario)
            double deltaAngle = endAngle - startAngle;
            if (command == "G2" && deltaAngle > 0) deltaAngle -= 2 * M_PI;
            if (command == "G3" && deltaAngle < 0) deltaAngle += 2 * M_PI;

            for (int i = 1; i <= segments; ++i) {
                double angle = startAngle + deltaAngle * i / segments;
                double px = cx + radius * std::cos(angle);
                double py = cy + radius * std::sin(angle);
                driver_->motion()->moveTo(px, py, -1, f);
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
            double x0 = 0, y0 = 0;

            for (int s = 1; s <= segments; ++s) {
                double t = static_cast<double>(s) / segments;
                double u = 1.0 - t;

                double px = u * u * u * x0 + 3 * u * u * t * i + 3 * u * t * t * p + t * t * t * x;
                double py = u * u * u * y0 + 3 * u * u * t * j + 3 * u * t * t * q + t * t * t * y;

                driver_->motion()->moveTo(px, py, -1, f);
            }
        }

    }

} // namespace translator::gcode
