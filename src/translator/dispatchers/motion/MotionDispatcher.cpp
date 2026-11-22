#include "translator/dispatchers/motion/MotionDispatcher.hpp"
#include "translator/exceptions/GCodeTranslatorInvalidCommandException.hpp"
#include "logger/Logger.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

#include "core/printer/state/StateTracker.hpp"

namespace translator::gcode {
    namespace {
        constexpr double PI = 3.14159265358979323846;

        using core::command::motion::MoveTarget;
        using Point = MotionDispatcher::Point;

        std::optional<double> param(const std::map<std::string, double> &params, const char *key) {
            const auto it = params.find(key);
            if (it == params.end()) return std::nullopt;
            return it->second;
        }

        double distance(Point a, Point b) {
            return std::hypot(b.x - a.x, b.y - a.y);
        }
    }

    MotionDispatcher::MotionDispatcher(std::shared_ptr<core::DriverInterface> driver)
        : driver_(std::move(driver)) {
    }

    bool MotionDispatcher::canHandle(const std::string &command) const {
        return command == "G0" || command == "G1" || command == "G2" || command == "G3" || command == "G5" ||
               command == "G28" || command == "G90" || command == "G91" || command == "G92" ||
               command == "M82" || command == "M83" || command == "M114" ||
               command == "G220" || command == "G999";
    }

    bool MotionDispatcher::validate(const std::string &command, const std::map<std::string, double> &params) const {
        if (command == "G0" || command == "G1") {
            // Anche "G1 E-2 F1800" (ritrazione) e "G1 F1500" (solo velocita') sono validi
            return params.count("X") || params.count("Y") || params.count("Z") ||
                   params.count("E") || params.count("F");
        }
        if (command == "G220") {
            return params.count("X") || params.count("Y") || params.count("Z");
        }
        if (command == "G2" || command == "G3") {
            return params.count("I") || params.count("J");
        }
        if (command == "G5") {
            // I/J possono mancare solo se il comando precedente era un altro G5
            const bool hasFirstControl = params.count("I") && params.count("J");
            return params.count("X") && params.count("Y") && params.count("P") && params.count("Q") &&
                   (hasFirstControl || lastSplineControl_.has_value());
        }
        return true;
    }

    // ------------------------------------------------------------------
    // Geometria
    // ------------------------------------------------------------------

    std::vector<Point> MotionDispatcher::flattenArc(Point start, Point end, Point center, bool clockwise) {
        const double radius = distance(center, start);
        std::vector<Point> points;
        if (radius < 1e-6) {
            points.push_back(end);
            return points;
        }

        const double startAngle = std::atan2(start.y - center.y, start.x - center.x);
        const double endAngle = std::atan2(end.y - center.y, end.x - center.x);

        // Inizio e fine coincidenti = cerchio completo
        double sweep;
        if (distance(start, end) < 1e-6) {
            sweep = clockwise ? -2.0 * PI : 2.0 * PI;
        } else {
            sweep = endAngle - startAngle;
            if (clockwise && sweep >= 0.0) sweep -= 2.0 * PI;
            if (!clockwise && sweep <= 0.0) sweep += 2.0 * PI;
        }

        // Errore di corda: e = r * (1 - cos(dTheta / 2))
        double maxStep = 2.0 * PI;
        if (radius > CURVE_TOLERANCE_MM) {
            maxStep = 2.0 * std::acos(1.0 - CURVE_TOLERANCE_MM / radius);
        }
        const double arcLength = std::fabs(sweep) * radius;

        int segments = static_cast<int>(std::ceil(std::fabs(sweep) / maxStep));
        const int byLength = std::max(1, static_cast<int>(std::floor(arcLength / MIN_SEGMENT_MM)));
        segments = std::min(segments, byLength);
        // Un arco non degenere ha comunque bisogno di qualche segmento per non diventare una corda
        if (std::fabs(sweep) > PI / 2.0) segments = std::max(segments, 8);
        segments = std::clamp(segments, 1, MAX_SEGMENTS);

        points.reserve(segments);
        for (int s = 1; s < segments; ++s) {
            const double angle = startAngle + sweep * s / segments;
            points.push_back({center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)});
        }
        points.push_back(end); // il punto finale e' quello richiesto, senza errori di arrotondamento
        return points;
    }

    std::vector<Point> MotionDispatcher::flattenBezier(Point p0, Point p1, Point p2, Point p3) {
        // Per una cubica campionata uniformemente in n tratti lo scostamento massimo e'
        // limitato da max|B''| / (8 n^2), con max|B''| = 6 * max(|p0-2p1+p2|, |p1-2p2+p3|).
        const double d1 = std::hypot(p0.x - 2 * p1.x + p2.x, p0.y - 2 * p1.y + p2.y);
        const double d2 = std::hypot(p1.x - 2 * p2.x + p3.x, p1.y - 2 * p2.y + p3.y);
        const double curvatureBound = 6.0 * std::max(d1, d2);

        int segments = static_cast<int>(std::ceil(std::sqrt(curvatureBound / (8.0 * CURVE_TOLERANCE_MM))));

        const double polygonLength = distance(p0, p1) + distance(p1, p2) + distance(p2, p3);
        const int byLength = std::max(1, static_cast<int>(std::floor(polygonLength / MIN_SEGMENT_MM)));
        segments = std::clamp(std::min(segments, byLength), 1, MAX_SEGMENTS);

        std::vector<Point> points;
        points.reserve(segments);
        for (int s = 1; s < segments; ++s) {
            const double t = static_cast<double>(s) / segments;
            const double u = 1.0 - t;
            const double b0 = u * u * u;
            const double b1 = 3 * u * u * t;
            const double b2 = 3 * u * t * t;
            const double b3 = t * t * t;
            points.push_back({b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x,
                              b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y});
        }
        points.push_back(p3);
        return points;
    }

    // ------------------------------------------------------------------
    // Stato
    // ------------------------------------------------------------------

    position::Position MotionDispatcher::currentPosition() {
        if (!position_) {
            // M114 riporta la posizione istantanea: serve quella a movimenti conclusi,
            // cioe' il punto da cui partira' il prossimo segmento.
            if (!driver_->waitForIdle()) {
                throw std::runtime_error("machine did not stop");
            }
            position_ = driver_->motion()->getPosition();
            if (!position_) {
                throw std::runtime_error("cannot read machine position");
            }
        }
        return *position_;
    }

    void MotionDispatcher::ensurePrinting() {
        const core::PrintState state = driver_->getState();
        if (state == core::PrintState::Printing) return;
        if (state != core::PrintState::Idle) {
            throw std::runtime_error("machine not ready for motion (state: " +
                                     core::DriverInterface::printStateToString(state) + ")");
        }
        const auto result = driver_->system()->startPrint();
        if (!result.isSuccess()) {
            throw std::runtime_error("cannot enter printing state: " + result.message);
        }
    }

    std::optional<double> MotionDispatcher::extrusionDelta(const std::map<std::string, double> &params) {
        const auto e = param(params, "E");
        if (!e) return std::nullopt;

        const double delta = absoluteE_ ? (*e - logicalE_) : *e;
        logicalE_ += delta;
        core::state::StateTracker::getInstance().updateEPosition(logicalE_);

        if (std::fabs(delta) < 1e-7) return std::nullopt;
        return delta;
    }

    void MotionDispatcher::trackLayer(const std::map<std::string, double> &params) {
        const auto z = param(params, "Z");
        if (!z || !absoluteXYZ_) return;

        auto &stateTracker = core::state::StateTracker::getInstance();
        if (*z > lastLayerZ_ + 0.01) {
            stateTracker.incrementLayer();
            stateTracker.setLayerHeight(*z - lastLayerZ_);
        }
        lastLayerZ_ = *z;
    }

    // ------------------------------------------------------------------
    // Movimenti
    // ------------------------------------------------------------------

    void MotionDispatcher::linearMove(const std::map<std::string, double> &params) {
        MoveTarget target;
        target.x = param(params, "X");
        target.y = param(params, "Y");
        target.z = param(params, "Z");
        target.e = extrusionDelta(params);

        if (!target.x && !target.y && !target.z && !target.e) {
            return; // solo F: gia' memorizzato
        }

        ensurePrinting();
        target.feedrate = feedrateToSend();

        const auto result = absoluteXYZ_ ? driver_->motion()->moveAbsolute(target)
                                         : driver_->motion()->moveRelative(target);
        if (!result.isSuccess()) {
            position_.reset();
            lastSentFeedrate_ = -1.0;
            publishState();
            throw std::runtime_error("move failed: " + result.message);
        }

        if (position_) {
            if (absoluteXYZ_) {
                if (target.x) position_->x = *target.x;
                if (target.y) position_->y = *target.y;
                if (target.z) position_->z = *target.z;
            } else {
                if (target.x) position_->x += *target.x;
                if (target.y) position_->y += *target.y;
                if (target.z) position_->z += *target.z;
            }
        } else if (absoluteXYZ_ && target.x && target.y && target.z) {
            position_ = position::Position{*target.x, *target.y, *target.z};
        }
    }

    void MotionDispatcher::sendPath(const position::Position &start, const std::vector<Point> &points,
                                    std::optional<double> targetZ, std::optional<double> extrusion) {
        // Lunghezza totale: Z ed E vengono distribuiti in proporzione al percorso
        double total = 0.0;
        Point previous{start.x, start.y};
        for (const auto &p: points) {
            total += distance(previous, p);
            previous = p;
        }

        ensurePrinting();

        const uint64_t epoch = driver_->faultEpoch();
        double travelled = 0.0;
        double extruded = 0.0;
        previous = {start.x, start.y};

        for (size_t i = 0; i < points.size(); ++i) {
            const Point &p = points[i];
            travelled += distance(previous, p);
            previous = p;

            const bool last = (i + 1 == points.size());
            const double fraction = last || total <= 0.0 ? 1.0 : travelled / total;

            MoveTarget target;
            target.x = p.x;
            target.y = p.y;
            if (targetZ) target.z = start.z + (*targetZ - start.z) * fraction;
            if (extrusion) {
                const double upTo = *extrusion * fraction;
                target.e = upTo - extruded;
                extruded = upTo;
            }
            target.feedrate = feedrateToSend();

            const auto result = driver_->motion()->moveAbsolute(target);
            if (!result.isSuccess() || driver_->faultEpoch() != epoch) {
                position_.reset();
                lastSentFeedrate_ = -1.0;
                publishState();
                throw std::runtime_error("curve interrupted: " + result.message);
            }

            position_ = position::Position{p.x, p.y, target.z ? *target.z : start.z};
        }
    }

    std::optional<double> MotionDispatcher::feedrateToSend() {
        // Dopo una risincronizzazione il firmware puo' essersi riavviato: F va rimandato
        const uint32_t epoch = driver_->sequenceEpoch();
        if (epoch != feedrateSequenceEpoch_) {
            feedrateSequenceEpoch_ = epoch;
            lastSentFeedrate_ = -1.0;
        }
        if (std::fabs(feedrate_ - lastSentFeedrate_) < 0.5) return std::nullopt;
        lastSentFeedrate_ = feedrate_;
        return feedrate_;
    }

    void MotionDispatcher::publishState() const {
        core::state::StateTracker::MotionSnapshot snapshot;
        snapshot.positionKnown = position_.has_value();
        if (position_) {
            snapshot.x = position_->x;
            snapshot.y = position_->y;
            snapshot.z = position_->z;
        }
        snapshot.absoluteXYZ = absoluteXYZ_;
        snapshot.absoluteE = absoluteE_;
        core::state::StateTracker::getInstance().setMotionSnapshot(snapshot);
    }

    void MotionDispatcher::home() {
        // Il firmware accetta l'homing solo da Idle
        const core::PrintState state = driver_->getState();
        if (state == core::PrintState::Printing || state == core::PrintState::Paused) {
            const auto ended = driver_->system()->endPrint();
            if (!ended.isSuccess()) {
                throw std::runtime_error("cannot leave printing state: " + ended.message);
            }
        }

        position_.reset();
        lastSplineControl_.reset();

        const auto result = driver_->system()->homing();
        if (!result.isSuccess()) {
            publishState();
            throw std::runtime_error("homing failed: " + result.message);
        }
        lastLayerZ_ = 0.0;
        position_ = driver_->motion()->getPosition();
    }

    void MotionDispatcher::setPosition(const std::map<std::string, double> &params) {
        const bool none = !params.count("X") && !params.count("Y") && !params.count("Z") && !params.count("E");

        if (params.count("E") || none) {
            logicalE_ = none ? 0.0 : params.at("E");
            core::state::StateTracker::getInstance().updateEPosition(logicalE_);
        }

        MoveTarget position;
        position.x = none ? std::optional<double>(0.0) : param(params, "X");
        position.y = none ? std::optional<double>(0.0) : param(params, "Y");
        position.z = none ? std::optional<double>(0.0) : param(params, "Z");
        if (!position.x && !position.y && !position.z) return; // solo E: e' una grandezza del driver

        const auto result = driver_->motion()->setPosition(position);
        if (!result.isSuccess()) {
            position_.reset();
            throw std::runtime_error("set position failed: " + result.message);
        }
        if (position_) {
            if (position.x) position_->x = *position.x;
            if (position.y) position_->y = *position.y;
            if (position.z) position_->z = *position.z;
        } else if (position.x && position.y && position.z) {
            position_ = position::Position{*position.x, *position.y, *position.z};
        }
    }

    void MotionDispatcher::handle(const std::string &command, const std::map<std::string, double> &params) {
        auto &stateTracker = core::state::StateTracker::getInstance();

        if (const auto f = param(params, "F"); f && *f > 0.0 && command != "G220") {
            feedrate_ = *f;
            stateTracker.updateFeedRate(feedrate_);
        }

        std::ostringstream cmdStr;
        cmdStr << command;
        for (const auto &[key, value]: params) {
            cmdStr << " " << key << value;
        }
        stateTracker.updateLastCommand(cmdStr.str());
        stateTracker.incrementCommandCount();

        const bool isSpline = (command == "G5");

        if (command == "G0" || command == "G1") {
            trackLayer(params);
            linearMove(params);
        } else if (command == "G2" || command == "G3") {
            const auto start = currentPosition();
            const Point from{start.x, start.y};

            // X/Y assenti = stesso punto (cerchio completo); in G91 sono relativi
            Point to = from;
            if (const auto x = param(params, "X")) to.x = absoluteXYZ_ ? *x : from.x + *x;
            if (const auto y = param(params, "Y")) to.y = absoluteXYZ_ ? *y : from.y + *y;

            const Point center{from.x + param(params, "I").value_or(0.0), from.y + param(params, "J").value_or(0.0)};

            const double startRadius = distance(center, from);
            const double endRadius = distance(center, to);
            if (startRadius < 1e-6 || std::fabs(startRadius - endRadius) > 0.5) {
                throw GCodeTranslatorInvalidCommandException(
                    command + ": arc endpoint not on the circle (r=" + std::to_string(startRadius) + " vs " +
                    std::to_string(endRadius) + ")");
            }

            std::optional<double> targetZ;
            if (const auto z = param(params, "Z")) targetZ = absoluteXYZ_ ? *z : start.z + *z;

            trackLayer(params);
            sendPath(start, flattenArc(from, to, center, command == "G2"), targetZ, extrusionDelta(params));
        } else if (isSpline) {
            const auto start = currentPosition();
            const Point p0{start.x, start.y};
            const Point p3{absoluteXYZ_ ? params.at("X") : p0.x + params.at("X"),
                           absoluteXYZ_ ? params.at("Y") : p0.y + params.at("Y")};

            // I,J: offset del primo punto di controllo rispetto all'inizio.
            // Se mancano, e' lo specchio del secondo punto di controllo del G5 precedente.
            Point p1;
            if (params.count("I") && params.count("J")) {
                p1 = {p0.x + params.at("I"), p0.y + params.at("J")};
            } else {
                p1 = {2.0 * p0.x - lastSplineControl_->x, 2.0 * p0.y - lastSplineControl_->y};
            }
            // P,Q: offset del secondo punto di controllo rispetto alla fine
            const Point p2{p3.x + params.at("P"), p3.y + params.at("Q")};

            std::optional<double> targetZ;
            if (const auto z = param(params, "Z")) targetZ = absoluteXYZ_ ? *z : start.z + *z;

            trackLayer(params);
            sendPath(start, flattenBezier(p0, p1, p2, p3), targetZ, extrusionDelta(params));
            lastSplineControl_ = p2;
        } else if (command == "G28") {
            home();
        } else if (command == "G90") {
            absoluteXYZ_ = true;
            absoluteE_ = true;
        } else if (command == "G91") {
            absoluteXYZ_ = false;
            absoluteE_ = false;
        } else if (command == "M82") {
            absoluteE_ = true;
        } else if (command == "M83") {
            absoluteE_ = false;
        } else if (command == "G92") {
            setPosition(params);
        } else if (command == "G220") {
            position_.reset();
            if (params.count("X")) driver_->motion()->diagnoseAxis("X", params.at("X"));
            if (params.count("Y")) driver_->motion()->diagnoseAxis("Y", params.at("Y"));
            if (params.count("Z")) driver_->motion()->diagnoseAxis("Z", params.at("Z"));
        } else if (command == "G999") {
            driver_->motion()->emergencyStop();
        } else if (command == "M114") {
            position_ = driver_->motion()->getPosition();
        }

        if (!isSpline) {
            lastSplineControl_.reset();
        }
        publishState();
    }
} // namespace translator::gcode
