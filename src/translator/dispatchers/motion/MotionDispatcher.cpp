#include "translator/dispatchers/motion/MotionDispatcher.hpp"
#include "core/utils/FloatFormatter.hpp"
#include <iostream>
#include <cmath>

namespace translator::gcode {
    MotionDispatcher::MotionDispatcher(std::shared_ptr<core::DriverInterface> driver)
        : driver_(std::move(driver)) {
    }

    bool MotionDispatcher::canHandle(const std::string &command) const {
        return command == "G0" || command == "G1" || command == "G220" || command == "G999" ||
               command == "G2" || command == "G3" || command == "G5" || command == "M114";
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

    inline double normalizeAngle(double angle) {
        while (angle <= -M_PI) angle += 2 * M_PI;
        while (angle > M_PI) angle -= 2 * M_PI;
        return angle;
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
            driver_->system()->startPrint();

            double x = params.at("X");
            double y = params.at("Y");
            double i = params.at("I");
            double j = params.at("J");
            double f = params.count("F") ? params.at("F") : 1000;

            position::Position pos = driver_->motion()->getPosition().value();
            double startX = pos.x;
            double startY = pos.y;
            double startZ = pos.z;

            double cx = startX + i;
            double cy = startY + j;
            double radius = std::hypot(i, j);

            double startAngle = std::atan2(startY - cy, startX - cx);
            double endAngle = std::atan2(y - cy, x - cx);
            double deltaAngle = normalizeAngle(endAngle - startAngle);

            if (command == "G2" && deltaAngle > 0) deltaAngle -= 2 * M_PI;
            if (command == "G3" && deltaAngle < 0) deltaAngle += 2 * M_PI;

            constexpr int segments = 40;
            double dz = 0;
            if (params.count("Z")) {
                dz = (params.at("Z") - startZ) / segments;
            }

            for (int s = 1; s <= segments; ++s) {
                double angle = startAngle + deltaAngle * s / segments;
                double px = cx + radius * std::cos(angle);
                double py = cy + radius * std::sin(angle);
                double pz = startZ + dz * s;

                if (!std::isfinite(px) || !std::isfinite(py)) continue;
                driver_->motion()->goTo(px, py, pz, f);
            }
        } else if (command == "G5") {
            driver_->system()->startPrint();

            double x = params.at("X");
            double y = params.at("Y");
            double i = params.at("I");
            double j = params.at("J");
            double p = params.at("P");
            double q = params.at("Q");
            double f = params.count("F") ? params.at("F") : 1000;

            position::Position pos = driver_->motion()->getPosition().value();
            double x0 = pos.x;
            double y0 = pos.y;
            double z0 = pos.z;

            constexpr int segments = 40;
            double dz = 0;
            if (params.count("Z")) {
                dz = (params.at("Z") - z0) / segments;
            }

            for (int s = 1; s <= segments; ++s) {
                double t = static_cast<double>(s) / segments;
                double u = 1 - t;

                double px = u * u * u * x0 + 3 * u * u * t * i + 3 * u * t * t * p + t * t * t * x;
                double py = u * u * u * y0 + 3 * u * u * t * j + 3 * u * t * t * q + t * t * t * y;
                double pz = z0 + dz * s;

                if (!std::isfinite(px) || !std::isfinite(py)) continue;
                driver_->motion()->goTo(px, py, pz, f);
            }
        } else if (command == "M114") {
            driver_->motion()->getPosition();
        }
    }
} // namespace translator::gcode
