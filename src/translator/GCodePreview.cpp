#include "translator/GCodePreview.hpp"

#include "core/MoveFrame.hpp"
#include "translator/GCodeSupport.hpp"
#include "translator/dispatchers/motion/MotionDispatcher.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>

namespace translator::gcode {
    namespace {
        using Point = MotionDispatcher::Point;
        using core::planning::PlannedBlock;

        /// Stato del percorso mentre si legge il file: fa le stesse scelte del driver in stampa.
        class Walker {
        public:
            Walker(Preview &preview, const core::planning::MachineLimits &limits, double advanceK,
                   double speedFactor, double blockRate)
                    : preview_(preview), limits_(limits) {
                planner_.configure(limits);
                planner_.setLinearAdvance(advanceK);
                planner_.setSpeedFactor(speedFactor);
                planner_.setBlockRate(blockRate);
                preview_.points.push_back({}); // partenza: origine, tempo zero
                duration_.push_back(0.0);
            }

            void line(const std::string &raw) {
                // Stessa regola con cui il job conta le righe eseguite
                if (raw.empty() || raw.find_first_not_of(" \t\r") == std::string::npos || raw[0] == ';' ||
                    raw[0] == '%') {
                    return;
                }
                ++command_;
                const std::string name = commandOf(raw);
                if (name.empty()) return;

                std::map<char, double> p;
                {
                    std::istringstream stream(raw.substr(0, raw.find(';')));
                    std::string token;
                    stream >> token;
                    while (stream >> token) {
                        if (token.size() < 2) continue;
                        try {
                            p[static_cast<char>(std::toupper(static_cast<unsigned char>(token[0])))] =
                                    std::stod(token.substr(1));
                        } catch (...) {}
                    }
                }
                const auto get = [&p](char key) -> std::optional<double> {
                    const auto it = p.find(key);
                    if (it == p.end()) return std::nullopt;
                    return it->second;
                };

                const bool isMove = name == "G0" || name == "G1" || name == "G2" || name == "G3" || name == "G5";
                if (isMove) {
                    if (const auto f = get('F'); f && *f > 0.0) feed_ = *f;
                }
                if (name != "G5") lastControl_.reset();

                if (name == "G0" || name == "G1") {
                    const double nx = get('X') ? (absolute_ ? *get('X') : x_ + *get('X')) : x_;
                    const double ny = get('Y') ? (absolute_ ? *get('Y') : y_ + *get('Y')) : y_;
                    const double nz = get('Z') ? (absolute_ ? *get('Z') : z_ + *get('Z')) : z_;
                    moveTo(nx, ny, nz, extrusion(get('E')));
                } else if (name == "G2" || name == "G3") {
                    const Point from{x_, y_};
                    Point to = from;
                    if (const auto v = get('X')) to.x = absolute_ ? *v : from.x + *v;
                    if (const auto v = get('Y')) to.y = absolute_ ? *v : from.y + *v;
                    const Point center{from.x + get('I').value_or(0.0), from.y + get('J').value_or(0.0)};
                    std::optional<double> targetZ;
                    if (const auto v = get('Z')) targetZ = absolute_ ? *v : z_ + *v;
                    path(MotionDispatcher::flattenArc(from, to, center, name == "G2"), targetZ, extrusion(get('E')));
                } else if (name == "G5") {
                    if (!get('X') || !get('Y') || !get('P') || !get('Q')) return;
                    const Point p0{x_, y_};
                    const Point p3{absolute_ ? *get('X') : p0.x + *get('X'), absolute_ ? *get('Y') : p0.y + *get('Y')};
                    Point p1;
                    if (get('I') && get('J')) p1 = {p0.x + *get('I'), p0.y + *get('J')};
                    else if (lastControl_) p1 = {2.0 * p0.x - lastControl_->x, 2.0 * p0.y - lastControl_->y};
                    else return;
                    const Point p2{p3.x + *get('P'), p3.y + *get('Q')};
                    std::optional<double> targetZ;
                    if (const auto v = get('Z')) targetZ = absolute_ ? *v : z_ + *v;
                    path(MotionDispatcher::flattenBezier(p0, p1, p2, p3), targetZ, extrusion(get('E')));
                    lastControl_ = p2;
                } else if (name == "G90") {
                    absolute_ = absoluteE_ = true;
                } else if (name == "G91") {
                    absolute_ = absoluteE_ = false;
                } else if (name == "M82") {
                    absoluteE_ = true;
                } else if (name == "M83") {
                    absoluteE_ = false;
                } else if (name == "M900") {
                    if (const auto k = get('K'); k && *k >= 0.0) planner_.setLinearAdvance(*k);
                } else if (name == "M204") {
                    double print = 0.0, travel = 0.0;
                    if (const auto v = get('S')) print = travel = *v;
                    if (const auto v = get('P')) print = *v;
                    if (const auto v = get('T')) travel = *v;
                    planner_.setAccelerations(print, travel);
                } else if (name == "G92") {
                    const bool none = !get('X') && !get('Y') && !get('Z') && !get('E');
                    if (get('E') || none) logicalE_ = none ? 0.0 : *get('E');
                    if (none || get('X') || get('Y') || get('Z')) {
                        stop(); // il driver manda un comando al firmware: il piano si chiude
                        if (none || get('X')) setAxis(0, none ? 0.0 : *get('X'));
                        if (none || get('Y')) setAxis(1, none ? 0.0 : *get('Y'));
                        if (none || get('Z')) setAxis(2, none ? 0.0 : *get('Z'));
                    }
                } else if (name == "G28") {
                    stop();
                    ++preview_.homings;
                    const bool all = !get('X') && !get('Y') && !get('Z');
                    if (all || get('X')) setAxis(0, 0.0);
                    if (all || get('Y')) setAxis(1, 0.0);
                    if (all || get('Z')) setAxis(2, 0.0);
                    mark(); // la testina e' andata all'origine: tratto di spostamento
                } else if (classify(name).support == Support::Executed) {
                    // Ogni altro comando eseguito va al firmware, e prima il driver chiude il piano
                    stop();
                    if (name == "G4") {
                        double wait = 0.0;
                        if (const auto v = get('P')) wait = *v / 1000.0;
                        if (const auto v = get('S')) wait = *v;
                        if (wait > 0.0) {
                            duration_.back() += wait;
                            preview_.dwellSeconds += wait;
                        }
                    } else if (name == "M109") {
                        ++preview_.heatWaits;
                    }
                }
            }

            /// Punto di partenza diverso dall'origine (percorsi relativi, come quelli dei test)
            void startAt(double x, double y, double z) {
                if (preview_.points.size() != 1) return; // solo prima di qualsiasi movimento
                setAxis(0, x);
                setAxis(1, y);
                setAxis(2, z);
                preview_.points[0].x = static_cast<float>(x);
                preview_.points[0].y = static_cast<float>(y);
                preview_.points[0].z = static_cast<float>(z);
            }

            void finish() {
                stop();
                double total = 0.0;
                for (size_t i = 0; i < preview_.points.size(); ++i) {
                    total += duration_[i];
                    preview_.points[i].seconds = total;
                }
                preview_.motionSeconds = total - preview_.dwellSeconds;
            }

        private:
            Preview &preview_;
            core::planning::MachineLimits limits_;
            core::planning::MotionPlanner planner_;
            std::vector<double> duration_; ///< secondi per arrivare a ciascun punto dal precedente
            std::vector<PlannedBlock> blocks_;

            bool absolute_ = true, absoluteE_ = true;
            double x_ = 0, y_ = 0, z_ = 0, logicalE_ = 0, feed_ = 1000.0;
            long long steps_[3] = {0, 0, 0};
            double eRemainder_ = 0.0;
            uint32_t command_ = 0;
            std::optional<Point> lastControl_;

            void collect() {
                for (const PlannedBlock &block: blocks_) {
                    if (block.tag < duration_.size()) duration_[block.tag] += block.seconds();
                }
                blocks_.clear();
            }

            void stop() {
                planner_.flush(blocks_);
                collect();
            }

            void setAxis(int axis, double value) {
                (axis == 0 ? x_ : axis == 1 ? y_ : z_) = value;
                steps_[axis] = std::llround(value * limits_.stepsPerMm[axis]);
            }

            void mark(bool extruding = false) {
                PreviewPoint point;
                point.x = static_cast<float>(x_);
                point.y = static_cast<float>(y_);
                point.z = static_cast<float>(z_);
                point.command = command_;
                point.extruding = extruding;
                preview_.points.push_back(point);
                duration_.push_back(0.0);
            }

            /// mm di filamento chiesti dal parametro E (0 se assente)
            double extrusion(std::optional<double> e) {
                if (!e) return 0.0;
                const double delta = absoluteE_ ? *e - logicalE_ : *e;
                logicalE_ += delta;
                return std::fabs(delta) < 1e-7 ? 0.0 : delta;
            }

            void moveTo(double nx, double ny, double nz, double extruded) {
                long long delta[4] = {0, 0, 0, 0};
                const double target[3] = {nx, ny, nz};
                bool cartesian = false;
                for (int a = 0; a < 3; ++a) {
                    delta[a] = std::llround(target[a] * limits_.stepsPerMm[a]) - steps_[a];
                    if (delta[a] != 0) cartesian = true;
                }
                if (extruded != 0.0) {
                    const double exact = extruded * limits_.stepsPerMm[3] + eRemainder_;
                    delta[3] = std::llround(exact);
                    eRemainder_ = exact - static_cast<double>(delta[3]);
                }
                x_ = nx;
                y_ = ny;
                z_ = nz;

                long long longest = 0;
                for (const long long d: delta) longest = std::max(longest, std::llabs(d));
                if (longest == 0) return;
                for (int a = 0; a < 3; ++a) steps_[a] += delta[a];

                mark(cartesian && delta[3] > 0);
                const uint32_t tag = static_cast<uint32_t>(preview_.points.size() - 1);

                // Come il driver: un movimento piu' lungo di un blocco parte in parti uguali
                const long long limit = core::MoveFrame::STEP_LIMIT;
                const long long parts = std::max(1LL, (longest + limit - 1) / limit);
                long long sent[4] = {0, 0, 0, 0};
                for (long long part = 1; part <= parts; ++part) {
                    long long chunk[4];
                    for (int a = 0; a < 4; ++a) {
                        const long long upTo = delta[a] * part / parts;
                        chunk[a] = upTo - sent[a];
                        sent[a] = upTo;
                    }
                    planner_.push(chunk, feed_, blocks_, tag);
                }
                collect();
            }

            void path(const std::vector<Point> &points, std::optional<double> targetZ, double extruded) {
                double total = 0.0;
                Point previous{x_, y_};
                for (const auto &p: points) {
                    total += std::hypot(p.x - previous.x, p.y - previous.y);
                    previous = p;
                }
                const double startZ = z_;
                double travelled = 0.0, done = 0.0;
                previous = {x_, y_};
                for (size_t i = 0; i < points.size(); ++i) {
                    travelled += std::hypot(points[i].x - previous.x, points[i].y - previous.y);
                    previous = points[i];
                    const double fraction = (i + 1 == points.size() || total <= 0.0) ? 1.0 : travelled / total;
                    const double upTo = extruded * fraction;
                    moveTo(points[i].x, points[i].y, targetZ ? startZ + (*targetZ - startZ) * fraction : startZ,
                           upTo - done);
                    done = upTo;
                }
            }
        };
    }

    Preview buildPreview(const std::string &path, const core::planning::MachineLimits &limits, double advanceK,
                         double speedFactor, double blockRate) {
        Preview preview;
        std::ifstream file(path);
        if (!file.is_open()) return preview;
        preview.readable = true;

        Walker walker(preview, limits, advanceK, speedFactor, blockRate);
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind(";START", 0) == 0) {
                // ";START X.. Y.. Z..": scritto dal servizio in testa ai file dei test
                double start[3] = {0, 0, 0};
                const char *names = "XYZ";
                for (int a = 0; a < 3; ++a) {
                    const size_t at = line.find(names[a], 6);
                    if (at != std::string::npos) start[a] = std::atof(line.c_str() + at + 1);
                }
                walker.startAt(start[0], start[1], start[2]);
                continue;
            }
            walker.line(line);
        }
        walker.finish();
        return preview;
    }

    std::string previewJson(const Preview &preview) {
        std::string out;
        out.reserve(preview.points.size() * 34 + 256);
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer),
                      "{\"ok\":%s,\"seconds\":%.1f,\"motionSeconds\":%.1f,\"dwellSeconds\":%.1f,"
                      "\"heatWaits\":%d,\"homings\":%d,\"count\":%zu,\"pts\":[",
                      preview.readable ? "true" : "false", preview.seconds(), preview.motionSeconds,
                      preview.dwellSeconds, preview.heatWaits, preview.homings, preview.points.size());
        out += buffer;
        bool first = true;
        for (const PreviewPoint &p: preview.points) {
            std::snprintf(buffer, sizeof(buffer), "%s%ld,%ld,%ld,%lld,%u,%d", first ? "" : ",",
                          std::lround(p.x * 100.0), std::lround(p.y * 100.0), std::lround(p.z * 100.0),
                          std::llround(p.seconds * 100.0), static_cast<unsigned>(p.command), p.extruding ? 1 : 0);
            out += buffer;
            first = false;
        }
        out += "]}";
        return out;
    }
}
