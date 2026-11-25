#include "Measure.hpp"

#include "MeasureMath.hpp"
#include "core/printer/PrintState.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <random>
#include <sstream>

namespace local {
    namespace {
        using core::command::motion::MoveTarget;

        /// La misura si ferma: annullata dall'operatore oppure non puo' proseguire.
        struct Stop {
            bool cancelled;
            std::string reason;
        };

        const char *const AXES = "XYZ";

        std::string quote(const std::string &text) {
            std::string out = "\"";
            for (const char c: text) {
                if (c == '"' || c == '\\') out.push_back('\\');
                if (static_cast<unsigned char>(c) >= 0x20) out.push_back(c);
            }
            return out + "\"";
        }

        std::string fmt(double value, int decimals = 1) {
            char buffer[40];
            std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
            return buffer;
        }

        std::string steps(long value) {
            return (value > 0 ? "+" : "") + std::to_string(value) + " step";
        }

        /// Limiti accettati per ogni parametro: fuori di qui la misura non parte.
        struct Range {
            const char *key;
            double min, max;
        };
        const Range RANGES[] = {
                {"n",        2,   50},
                {"distance", 1,   500},
                {"from",     1,   10000},
                {"to",       1,   10000},
                {"levels",   2,   20},
                {"repeats",  1,   100},
                {"tol",      1,   50},
                {"speed",    1,   200},
                {"seconds",  5,   300},
                {"target",   40,  270},
                {"hold",     20,  600},
                {"timeout",  60,  1800},
                {"minutes",  0.2, 720},
                {"check",    0.2, 60},
                {"seed",     0,   1e9},
        };

        const char *titleOf(const std::string &kind) {
            if (kind == "repeat") return "ripetibilita' del finecorsa";
            if (kind == "speed") return "velocita' massima senza perdere step";
            if (kind == "accel") return "accelerazione massima senza perdere step";
            if (kind == "travel") return "corsa dell'asse";
            if (kind == "tnoise") return "rumore del sensore di temperatura";
            if (kind == "heat") return "riscaldamento e tenuta della temperatura";
            if (kind == "stress") return "stress test della meccanica";
            if (kind == "pack") return "pacchetto di misure";
            return nullptr;
        }
    }

    Measure::Measure(Host host) : host_(std::move(host)) { timeScale_ = host_.timeScale; }

    Measure::~Measure() {
        cancel_ = true;
        if (worker_.joinable()) worker_.join();
    }

    void Measure::cancel() { cancel_ = true; }

    std::string Measure::start(const Params &params) {
        if (active_) return "a measurement is already running";
        const auto kind = params.find("kind");
        if (kind == params.end() || !titleOf(kind->second)) {
            return "measure: unknown kind (repeat, speed, accel, travel, tnoise, heat, stress, pack, steps)";
        }
        for (const auto &[key, value]: params) {
            if (key == "axis") {
                if (value != "x" && value != "y" && value != "z" && value != "X" && value != "Y" && value != "Z") {
                    return "measure: axis must be x, y or z";
                }
                continue;
            }
            for (const Range &r: RANGES) {
                if (key != r.key) continue;
                char *end = nullptr;
                const double v = std::strtod(value.c_str(), &end);
                if (end == value.c_str() || v < r.min || v > r.max) {
                    return "measure: " + key + " must be between " + fmt(r.min, 1) + " and " + fmt(r.max, 0);
                }
            }
        }
        if (kind->second == "pack") {
            const auto name = params.find("name");
            const std::string n = name == params.end() ? "" : name->second;
            if (n != "mechanics" && n != "thermal" && n != "all") return "measure: pack needs name=mechanics|thermal|all";
        }
        if (worker_.joinable()) worker_.join();

        std::string title = titleOf(kind->second);
        if (const auto axis = params.find("axis"); axis != params.end() && kind->second != "stress") {
            title += std::string(" ") + static_cast<char>(std::toupper(static_cast<unsigned char>(axis->second[0])));
        }
        if (kind->second == "pack") {
            const std::string n = params.at("name");
            title = n == "mechanics" ? "pacchetto meccanica" : n == "thermal" ? "pacchetto termico" : "pacchetto completo";
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            kind_ = kind->second;
            title_ = title;
            phase_ = "avvio";
            verdict_.clear();
            summary_.clear();
            progress_ = 0.0;
            startedAt_ = static_cast<long long>(std::time(nullptr));
            finishedAt_ = 0;
            rows_.clear();
            log_.clear();
            suggest_.clear();
            jobId_ = "measure-" + std::to_string(startedAt_);
        }
        PrintRecord record;
        record.jobId = jobId_;
        record.name = "Misura: " + title;
        record.state = "RUNNING";
        host_.history->add(record);

        cancel_ = false;
        active_ = true;
        worker_ = std::thread([this, params] { run(params); });
        return "";
    }

    // ------------------------------------------------------------------ passi comuni

    void Measure::guard() {
        if (cancel_) throw Stop{true, "annullata dall'operatore"};
        if (host_.driver->isLinkFaulted()) throw Stop{false, "collegamento perso: " + host_.driver->lastFaultReason()};
        if (host_.driver->getState() == core::PrintState::Error) {
            throw Stop{false, "la scheda e' in errore: " + host_.driver->lastFaultReason()};
        }
    }

    void Measure::phase(const std::string &text, double progress) {
        std::lock_guard<std::mutex> lock(mutex_);
        phase_ = text;
        if (progress >= 0.0) progress_ = std::min(1.0, progress);
    }

    void Measure::log(const std::string &text) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            log_.push_back(text);
            if (log_.size() > 400) log_.erase(log_.begin());
        }
        host_.history->addEvent(jobId_, "info", text);
    }

    void Measure::row(const std::string &label, const std::string &value, const std::string &verdict) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            rows_.push_back({section_, label, value, verdict});
        }
        host_.history->addEvent(jobId_, verdict == "bad" ? "error" : verdict == "warn" ? "warn" : "info",
                                section_ + " - " + label + ": " + value);
    }

    void Measure::suggest(const std::string &key, double value) {
        char buffer[40];
        std::snprintf(buffer, sizeof(buffer), "%.6g", value);
        std::lock_guard<std::mutex> lock(mutex_);
        suggest_[key] = buffer;
    }

    void Measure::wait(double machineSeconds) {
        const double until = host_.wire->machineSeconds() + machineSeconds;
        while (host_.wire->machineSeconds() < until) {
            guard();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    void Measure::settle() {
        host_.driver->motion()->flushPlanned();
        guard();
        if (!host_.driver->waitForIdle(std::chrono::minutes(10))) {
            guard();
            throw Stop{false, "la macchina non si e' fermata entro dieci minuti"};
        }
        guard();
    }

    void Measure::go(int axis, double position, double speed, bool pipelined) {
        guard();
        if (host_.driver->getState() == core::PrintState::Idle) {
            const auto started = host_.driver->system()->startPrint();
            if (!started.isSuccess()) throw Stop{false, "la scheda non accetta movimenti: " + started.message};
        }
        MoveTarget target;
        (axis == 0 ? target.x : axis == 1 ? target.y : target.z) = position;
        target.feedrate = speed * 60.0;
        const auto moved = host_.driver->motion()->moveAbsolute(target, pipelined);
        if (!moved.isSuccess()) {
            guard();
            throw Stop{false, std::string("movimento di ") + AXES[axis] + " rifiutato: " + moved.message};
        }
    }

    void Measure::goXY(double x, double y, double speed) {
        guard();
        if (host_.driver->getState() == core::PrintState::Idle) {
            const auto started = host_.driver->system()->startPrint();
            if (!started.isSuccess()) throw Stop{false, "la scheda non accetta movimenti: " + started.message};
        }
        MoveTarget target;
        target.x = x;
        target.y = y;
        target.feedrate = speed * 60.0;
        const auto moved = host_.driver->motion()->moveAbsolute(target, true);
        if (!moved.isSuccess()) {
            guard();
            throw Stop{false, "movimento rifiutato: " + moved.message};
        }
    }

    namespace {
        measure::Homing homeNow(const std::shared_ptr<core::DriverInterface> &driver, const std::string &axes) {
            const auto state = driver->getState();
            if (state == core::PrintState::Printing || state == core::PrintState::Paused) driver->system()->endPrint();
            const auto result = driver->system()->homeAxes(axes);
            const auto homing = measure::parseHoming(result.body);
            for (const char axis: axes) {
                const int a = axis - 'X';
                if (homing.failed[a]) throw Stop{false, std::string("il finecorsa di ") + axis + " non e' scattato"};
            }
            if (!result.isSuccess()) throw Stop{false, "homing non riuscito: " + result.message};
            return homing;
        }
    }

    void Measure::reference(const std::string &axes) {
        settle();
        const auto homing = homeNow(host_.driver, axes);
        for (const char axis: axes) {
            if (homing.skipped[axis - 'X']) {
                throw Stop{false, std::string("il finecorsa di ") + axis + " non e' abilitato nel firmware "
                                  "(EndstopConfig.hpp): senza finecorsa non c'e' nulla con cui misurare"};
            }
        }
    }

    long Measure::homeOffset(int axis) {
        settle();
        const auto homing = homeNow(host_.driver, std::string(1, AXES[axis]));
        if (homing.skipped[axis]) throw Stop{false, std::string("il finecorsa di ") + AXES[axis] + " non e' abilitato nel firmware"};
        if (!homing.offset[axis]) {
            throw Stop{false, "il firmware non riporta lo sfasamento del finecorsa: sulla scheda c'e' una versione "
                              "precedente, va ricaricata quella attuale"};
        }
        return *homing.offset[axis];
    }

    double Measure::travel(int axis) {
        const auto info = host_.driver->motion()->machineInfo();
        const char *keys[3] = {"volume_x", "volume_y", "volume_z"};
        const double configured = host_.settings->number(keys[axis]);
        return info.valid && info.travelMm[axis] > 0.0 ? std::min(configured, info.travelMm[axis]) : configured;
    }

    double Measure::temperature(double *target) {
        const auto result = host_.driver->temperature()->getHotendTemperature();
        double current = 0.0;
        for (const auto &line: result.body) {
            if (line.rfind("T:", 0) == 0) current = std::atof(line.c_str() + 2);
            else if (target && line.rfind("TARGET:", 0) == 0) *target = std::atof(line.c_str() + 7);
        }
        return current;
    }

    // ------------------------------------------------------------------ misure meccaniche

    bool Measure::repeatability(int axis, int count) {
        section_ = std::string("Ripetibilita' del finecorsa ") + AXES[axis];
        const auto limits = host_.driver->motion()->plannerLimits();
        const double away = axis == 2 ? 3.0 : 10.0, speed = std::min(50.0, limits.maxFeed[axis]);
        reference(std::string(1, AXES[axis]));

        std::vector<double> offsets;
        for (int i = 0; i < count; ++i) {
            phase(section_ + ": contatto " + std::to_string(i + 1) + " di " + std::to_string(count));
            go(axis, away, speed);
            offsets.push_back(static_cast<double>(homeOffset(axis)));
        }
        const auto s = measure::stats(offsets);
        const double micron = 1000.0 / limits.stepsPerMm[axis];
        const double spread = s.max - s.min;
        row("contatti", std::to_string(s.count));
        row("scarto medio", fmt(s.mean, 2) + " step (" + fmt(s.mean * micron, 0) + " micron)");
        row("deviazione standard", fmt(s.deviation, 2) + " step (" + fmt(s.deviation * micron, 0) + " micron)");
        row("dal minimo al massimo", fmt(spread, 0) + " step (" + fmt(spread * micron, 0) + " micron)",
            spread <= 2 ? "ok" : spread <= 4 ? "warn" : "bad");
        log(section_ + ": " + fmt(spread, 0) + " step tra il contatto piu' basso e il piu' alto");
        return spread <= 4;
    }

    bool Measure::speedLadder(int axis, double distance, double from, double to, int levels, int repeats,
                              long tolerance) {
        section_ = std::string("Velocita' massima ") + AXES[axis];
        const auto limits = host_.driver->motion()->plannerLimits();
        const double base = axis == 2 ? 2.0 : 5.0;
        const double room = travel(axis) - base - 1.0;
        distance = std::min(distance, room);
        to = std::min(to, limits.maxFeed[axis]);
        from = std::min(from, to);
        if (distance < 1.0) throw Stop{false, "corsa insufficiente per la prova"};
        const double accelTravel = host_.settings->number("accel_travel");
        const double acceleration = accelTravel > 0.0 ? std::min(accelTravel, limits.maxAcceleration[axis])
                                                      : limits.maxAcceleration[axis];
        reference(std::string(1, AXES[axis]));

        double clean = 0.0, lostAt = 0.0;
        long lost = 0;
        for (int i = 0; i < levels && lostAt == 0.0; ++i) {
            const double speed = levels > 1 ? from + (to - from) * i / (levels - 1) : to;
            const double reached = measure::peakSpeed(speed, acceleration, distance);
            phase(section_ + ": " + fmt(speed, 0) + " mm/s", static_cast<double>(i) / levels);
            for (int r = 0; r < repeats; ++r) {
                go(axis, base + distance, speed);
                go(axis, base, speed);
            }
            const long offset = homeOffset(axis);
            const bool ok = std::labs(offset) <= tolerance;
            std::string label = fmt(speed, 0) + " mm/s";
            if (reached < speed - 0.5) label += " (raggiunti " + fmt(reached, 0) + ": tratto corto)";
            row(label, steps(offset), ok ? "ok" : "bad");
            if (ok) clean = reached;
            else {
                lostAt = speed;
                lost = offset;
            }
        }
        if (lostAt > 0.0) {
            const double safe = std::floor(clean * 0.8);
            row("esito", "persi " + std::to_string(std::labs(lost)) + " step a " + fmt(lostAt, 0) + " mm/s", "bad");
            if (clean > 0.0) {
                row("limite consigliato", fmt(safe, 0) + " mm/s (80% dell'ultima velocita' pulita, " + fmt(clean, 0) + ")");
                suggest(axis == 2 ? "max_feed_z" : "max_feed_xy", safe);
            } else {
                row("limite consigliato", "nessuno: perde step gia' alla velocita' piu' bassa provata", "bad");
            }
            return false;
        }
        row("esito", "nessuno step perso fino a " + fmt(clean, 0) + " mm/s", "ok");
        return true;
    }

    bool Measure::accelLadder(int axis, double distance, double from, double to, int levels, int repeats,
                              long tolerance) {
        section_ = std::string("Accelerazione massima ") + AXES[axis];
        const auto limits = host_.driver->motion()->plannerLimits();
        const double base = axis == 2 ? 2.0 : 5.0;
        distance = std::min(distance, travel(axis) - base - 1.0);
        to = std::min(to, limits.maxAcceleration[axis]);
        from = std::min(from, to);
        if (distance < 1.0) throw Stop{false, "corsa insufficiente per la prova"};
        const double speed = limits.maxFeed[axis];
        reference(std::string(1, AXES[axis]));

        double clean = 0.0, lostAt = 0.0;
        long lost = 0;
        for (int i = 0; i < levels && lostAt == 0.0; ++i) {
            const double acceleration = levels > 1 ? from + (to - from) * i / (levels - 1) : to;
            phase(section_ + ": " + fmt(acceleration, 0) + " mm/s2", static_cast<double>(i) / levels);
            host_.driver->motion()->setAccelerations(acceleration, acceleration);
            for (int r = 0; r < repeats; ++r) {
                go(axis, base + distance, speed);
                go(axis, base, speed);
            }
            const long offset = homeOffset(axis);
            const bool ok = std::labs(offset) <= tolerance;
            row(fmt(acceleration, 0) + " mm/s2 (fino a " + fmt(measure::peakSpeed(speed, acceleration, distance), 0) +
                " mm/s)", steps(offset), ok ? "ok" : "bad");
            if (ok) clean = acceleration;
            else {
                lostAt = acceleration;
                lost = offset;
            }
        }
        host_.restore();
        if (lostAt > 0.0) {
            const double safe = std::floor(clean * 0.8 / 10.0) * 10.0;
            row("esito", "persi " + std::to_string(std::labs(lost)) + " step a " + fmt(lostAt, 0) + " mm/s2", "bad");
            if (clean > 0.0) {
                row("limite consigliato", fmt(safe, 0) + " mm/s2 (80% dell'ultima accelerazione pulita, " + fmt(clean, 0) + ")");
                suggest(axis == 2 ? "max_accel_z" : "max_accel_xy", safe);
            } else {
                row("limite consigliato", "nessuno: perde step gia' all'accelerazione piu' bassa provata", "bad");
            }
            return false;
        }
        row("esito", "nessuno step perso fino a " + fmt(clean, 0) + " mm/s2", "ok");
        return true;
    }

    bool Measure::travelCheck(int axis, double speed, long tolerance) {
        section_ = std::string("Corsa dell'asse ") + AXES[axis];
        const auto limits = host_.driver->motion()->plannerLimits();
        const double end = travel(axis), base = axis == 2 ? 2.0 : 5.0;
        speed = std::min(speed, limits.maxFeed[axis]);
        reference(std::string(1, AXES[axis]));
        phase(section_ + ": verso il fondo corsa dichiarato (" + fmt(end, 0) + " mm)", 0.2);
        go(axis, end, speed);
        phase(section_ + ": ritorno", 0.6);
        go(axis, base, speed);
        const long offset = homeOffset(axis);
        row("corsa dichiarata", fmt(end, 1) + " mm");
        if (std::labs(offset) <= tolerance) {
            row("esito", "percorsa tutta senza perdere step (" + steps(offset) + "): la corsa reale e' almeno " +
                         fmt(end, 1) + " mm", "ok");
            return true;
        }
        if (offset > 0) {
            // Il carrello ha trovato il fondo prima del previsto: il motore ha saltato, e al ritorno il
            // finecorsa e' arrivato in anticipo di tanti step quanti ne sono stati saltati.
            const double real = end - static_cast<double>(offset) / limits.stepsPerMm[axis];
            row("esito", "il carrello si e' fermato prima: persi " + std::to_string(offset) + " step", "bad");
            row("corsa reale stimata", fmt(real, 1) + " mm (un motore che salta perde un passo intero alla volta: "
                                       "l'incertezza e' di circa un millimetro)");
            const char *keys[3] = {"volume_x", "volume_y", "volume_z"};
            suggest(keys[axis], std::floor(real - 1.0));
        } else {
            row("esito", "persi " + std::to_string(-offset) + " step nel ritorno: non e' la lunghezza della corsa, "
                         "e' la meccanica che non regge il movimento", "bad");
        }
        return false;
    }

    // ------------------------------------------------------------------ misure termiche

    bool Measure::sensorNoise(double seconds) {
        section_ = "Sensore di temperatura a riposo";
        if (temperature() <= 0.0) throw Stop{false, "il termistore non e' attivo: nessuna temperatura da misurare"};
        std::vector<double> values;
        const double start = host_.wire->machineSeconds();
        while (host_.wire->machineSeconds() - start < seconds) {
            phase(section_, (host_.wire->machineSeconds() - start) / seconds);
            values.push_back(temperature());
            wait(0.5);
        }
        const auto s = measure::stats(values);
        row("letture", std::to_string(s.count));
        row("media", fmt(s.mean, 1) + " gradi");
        row("deviazione standard", fmt(s.deviation, 2) + " gradi", s.deviation <= 0.5 ? "ok" : s.deviation <= 1.5 ? "warn" : "bad");
        row("dal minimo al massimo", fmt(s.max - s.min, 1) + " gradi");
        return s.deviation <= 1.5;
    }

    bool Measure::heatTest(double target, double hold, double timeout) {
        section_ = "Riscaldamento a " + fmt(target, 0) + " gradi";
        constexpr double PID_RANGE = 10.0; // HEATER_PID_RANGE del firmware: sotto, l'heater e' sempre acceso
        const double ambient = temperature();
        if (ambient <= 0.0) throw Stop{false, "il termistore non e' attivo: non si puo' scaldare"};

        const auto set = host_.driver->temperature()->setHotendTemperature(static_cast<int>(target));
        if (!set.isSuccess()) throw Stop{false, "la scheda ha rifiutato la temperatura: " + set.message};
        heating_ = true;

        std::vector<measure::TempSample> samples;
        const double start = host_.wire->machineSeconds();
        double reachedAt = -1.0, peak = ambient;
        for (;;) {
            const double now = host_.wire->machineSeconds() - start;
            const double t = temperature();
            samples.push_back({now, t});
            peak = std::max(peak, t);
            if (reachedAt < 0.0 && t >= target - 1.0) reachedAt = now;
            if (reachedAt < 0.0) {
                phase(section_ + ": " + fmt(t, 0) + " gradi", 0.6 * std::min(1.0, (t - ambient) / std::max(1.0, target - ambient)));
                if (now > timeout) {
                    row("esito", "non arriva a " + fmt(target, 0) + " gradi in " + fmt(timeout, 0) + " s (ferma a " +
                                 fmt(t, 0) + ")", "bad");
                    return false;
                }
            } else {
                phase(section_ + ": tenuta, " + fmt(t, 1) + " gradi", 0.6 + 0.4 * (now - reachedAt) / hold);
                if (now - reachedAt >= hold) break;
            }
            wait(0.5);
        }
        host_.driver->temperature()->setHotendTemperature(0);
        heating_ = false;

        std::vector<double> regime;
        for (const auto &s: samples) {
            if (s.t >= reachedAt + hold / 3.0) regime.push_back(s.celsius - target);
        }
        const auto r = measure::stats(regime);
        const double overshoot = peak - target;
        row("partenza", fmt(ambient, 0) + " gradi");
        row("tempo per arrivare", fmt(reachedAt, 0) + " s");
        row("superamento massimo", fmt(std::max(0.0, overshoot), 1) + " gradi",
            overshoot <= 3.0 ? "ok" : overshoot <= 8.0 ? "warn" : "bad");
        row("a regime: errore medio", fmt(r.mean, 2) + " gradi", std::fabs(r.mean) <= 1.0 ? "ok" : "warn");
        row("a regime: oscillazione", fmt(r.max - r.min, 1) + " gradi dal minimo al massimo",
            r.max - r.min <= 2.0 ? "ok" : r.max - r.min <= 5.0 ? "warn" : "bad");

        const auto pid = measure::tuneFromRise(samples, target - PID_RANGE);
        if (pid.valid) {
            row("hotend: pendenza a piena potenza", fmt(pid.rate, 2) + " gradi/s");
            row("hotend: ritardo", fmt(pid.deadTime, 1) + " s");
            row("PID proposto", "P " + fmt(pid.kp, 1) + "  I " + fmt(pid.ki, 2) + "  D " + fmt(pid.kd, 0) +
                                " (da applicare e verificare ripetendo la prova)");
            suggest("pid_p", std::round(pid.kp * 10.0) / 10.0);
            suggest("pid_i", std::round(pid.ki * 100.0) / 100.0);
            suggest("pid_d", std::round(pid.kd));
        } else {
            row("PID proposto", "nessuno: " + pid.why);
        }
        return overshoot <= 8.0 && r.max - r.min <= 5.0;
    }

    // ------------------------------------------------------------------ stress test

    bool Measure::stress(double minutes, double checkMinutes, bool withZ, unsigned seed, long tolerance, bool force) {
        section_ = "Stress test";
        const auto limits = host_.driver->motion()->plannerLimits();
        const std::string axes = withZ ? "XYZ" : "XY";
        settle();
        bool blind = false;
        {
            const auto homing = homeNow(host_.driver, axes);
            for (const char axis: axes) blind = blind || homing.skipped[axis - 'X'];
        }
        if (blind && !force) {
            throw Stop{false, "i finecorsa non sono abilitati nel firmware: lo stress test puo' girare solo senza "
                              "verifica degli step (force=1). In quel caso la posizione attuale diventa l'origine: "
                              "portare prima il carrello nell'angolo dei finecorsa"};
        }
        if (blind) row("verifica degli step", "nessuna: finecorsa non abilitati, controllare a occhio che torni al punto di partenza", "warn");

        const double lo = 5.0, hiX = travel(0) - 5.0, hiY = travel(1) - 5.0, hiZ = std::min(20.0, travel(2) - 2.0);
        if (hiX - lo < 20.0 || hiY - lo < 20.0) throw Stop{false, "volume di lavoro troppo piccolo per lo stress test"};
        const double fast = std::min(limits.maxFeed[0], limits.maxFeed[1]);
        std::mt19937 random(seed);
        const auto pick = [&random](double a, double b) { return std::uniform_real_distribution<double>(a, b)(random); };

        double x = 1.25, y = 1.25, distance = 0.0;
        long moves = 0, reversals = 0, checks = 0, worst[3] = {0, 0, 0};
        const auto line = [&](double nx, double ny, double speed) {
            nx = std::clamp(nx, lo, hiX);
            ny = std::clamp(ny, lo, hiY);
            goXY(nx, ny, speed);
            distance += std::hypot(nx - x, ny - y);
            ++moves;
            x = nx;
            y = ny;
        };

        const double begin = host_.wire->machineSeconds();
        double lastCheck = begin;
        std::string since;      // cosa e' stato eseguito dall'ultima verifica
        bool passed = true;
        const auto verify = [&]() {
            if (blind) {
                settle();
                return true;
            }
            ++checks;
            std::string text;
            bool ok = true;
            for (const char axis: axes) {
                const int a = axis - 'X';
                const long offset = homeOffset(a);
                if (std::labs(offset) > std::labs(worst[a])) worst[a] = offset;
                text += std::string(" ") + axis + " " + steps(offset);
                ok = ok && std::labs(offset) <= tolerance;
            }
            const double at = (host_.wire->machineSeconds() - begin) / 60.0;
            log("verifica " + std::to_string(checks) + " a " + fmt(at, 1) + " min:" + text);
            if (!ok) row("verifica " + std::to_string(checks) + " a " + fmt(at, 1) + " min", text.substr(1) + " dopo: " + since, "bad");
            x = y = 1.25;
            since.clear();
            return ok;
        };

        while (passed) {
            const double now = host_.wire->machineSeconds();
            const double elapsed = (now - begin) / 60.0;
            if (elapsed >= minutes) break;
            phase("Stress test: " + fmt(elapsed, 1) + " di " + fmt(minutes, 1) + " minuti, " + fmt(distance / 1000.0, 1) + " m",
                  elapsed / minutes);
            if ((now - lastCheck) / 60.0 >= checkMinutes) {
                passed = verify();
                lastCheck = host_.wire->machineSeconds();
                continue;
            }

            const int pattern = std::uniform_int_distribution<int>(0, 3)(random);
            if (pattern == 0) {
                // corse lunghe alla velocita' massima: accelerazione piena e velocita' di punta
                since += "corse lunghe; ";
                for (int i = 0; i < 6; ++i) line(pick(lo, hiX), pick(lo, hiY), fast);
            } else if (pattern == 1) {
                // inversioni fitte: il passo cambia di volta in volta, quindi cambia la frequenza con cui
                // la macchina viene scossa. E' qui che si trovano le risonanze.
                const double lengths[] = {0.5, 1.0, 2.0, 5.0, 10.0, 20.0};
                const double length = lengths[std::uniform_int_distribution<int>(0, 5)(random)];
                const double angle = pick(0.0, 3.14159265);
                const double dx = std::cos(angle) * length, dy = std::sin(angle) * length;
                const double x0 = std::clamp(x, lo + 20.0, hiX - 20.0), y0 = std::clamp(y, lo + 20.0, hiY - 20.0);
                since += "inversioni da " + fmt(length, 1) + " mm; ";
                line(x0, y0, fast);
                for (int i = 0; i < 40; ++i) {
                    line(i % 2 == 0 ? x0 + dx : x0, i % 2 == 0 ? y0 + dy : y0, fast);
                    ++reversals;
                }
            } else if (pattern == 2) {
                // cerchi a corde corte: tanti movimenti piccoli di fila, come un perimetro curvo
                const double radius = pick(5.0, std::min(40.0, std::min(hiX - lo, hiY - lo) / 2.0 - 1.0));
                const double cx = pick(lo + radius, hiX - radius), cy = pick(lo + radius, hiY - radius);
                const int chords = std::max(24, static_cast<int>(2.0 * 3.14159265 * radius / 0.5));
                since += "cerchi di raggio " + fmt(radius, 0) + " mm; ";
                for (int i = 0; i <= chords * 2; ++i) {
                    const double a = 2.0 * 3.14159265 * i / chords;
                    line(cx + radius * std::cos(a), cy + radius * std::sin(a), fast);
                }
            } else {
                // scatti corti in direzioni a caso: non si arriva mai alla velocita' piena, lavora solo l'accelerazione
                since += "scatti corti; ";
                for (int i = 0; i < 30; ++i) line(x + pick(-3.0, 3.0), y + pick(-3.0, 3.0), fast);
            }
            if (withZ && std::uniform_int_distribution<int>(0, 3)(random) == 0) {
                host_.driver->motion()->flushPlanned();
                go(2, pick(2.0, hiZ), limits.maxFeed[2], true);
            }
        }
        if (passed) passed = verify();

        const double elapsed = (host_.wire->machineSeconds() - begin) / 60.0;
        row("durata", fmt(elapsed, 1) + " minuti");
        row("strada percorsa", fmt(distance / 1000.0, 1) + " m in " + std::to_string(moves) + " movimenti");
        row("inversioni secche", std::to_string(reversals));
        if (!blind) {
            row("verifiche con homing", std::to_string(checks));
            std::string text;
            for (const char axis: axes) text += std::string(text.empty() ? "" : ", ") + axis + " " + steps(worst[axis - 'X']);
            row("scarto piu' grande", text, passed ? "ok" : "bad");
        }
        row("esito", passed ? (blind ? "completato senza errori della scheda (step non verificati)"
                                     : "nessuno step perso")
                            : "la meccanica ha perso step: abbassare velocita' o accelerazione e ripetere", passed ? (blind ? "warn" : "ok") : "bad");
        return passed;
    }

    // ------------------------------------------------------------------ esecuzione

    void Measure::healthRows(const std::function<void()> &body) {
        const auto before = measure::parseHealth(host_.driver->system()->diagnostics().body);
        const auto wireBefore = host_.wire->live();
        body();
        settle();
        const auto after = measure::parseHealth(host_.driver->system()->diagnostics().body);
        const auto wireAfter = host_.wire->live();
        section_ = "Scheda e collegamento durante la misura";
        if (before.valid && after.valid) {
            const long missed = after.missedDeadlines - before.missedDeadlines;
            const long uart = after.uartErrors - before.uartErrors, checksum = after.checksumErrors - before.checksumErrors;
            row("scadenze degli step mancate", std::to_string(missed), missed == 0 ? "ok" : "bad");
            row("errori di ricezione sulla scheda", std::to_string(uart + checksum + after.overflows - before.overflows),
                uart + checksum == 0 ? "ok" : "warn");
            // sulla stampante simulata questi due non esistono (valgono zero)
            if (after.stepInterruptMaxUs > 0) {
                row("durata massima dell'interrupt degli step", std::to_string(after.stepInterruptMaxUs) + " microsecondi");
            }
            if (after.freeRamMin > 0) row("RAM minima rimasta libera", std::to_string(after.freeRamMin) + " byte");
        }
        const auto resent = wireAfter.resent - wireBefore.resent, errors = wireAfter.errors - wireBefore.errors;
        row("movimenti inviati", std::to_string(wireAfter.framesTx - wireBefore.framesTx));
        row("movimenti ritrasmessi", std::to_string(resent), resent == 0 ? "ok" : "warn");
        row("risposte di errore", std::to_string(errors), errors == 0 ? "ok" : "warn");
    }

    void Measure::run(Params params) {
        const auto number = [&params](const char *key, double fallback) {
            const auto it = params.find(key);
            return it == params.end() || it->second.empty() ? fallback : std::atof(it->second.c_str());
        };
        const auto axisOf = [&params]() {
            const auto it = params.find("axis");
            return it == params.end() ? 0 : std::toupper(static_cast<unsigned char>(it->second[0])) - 'X';
        };
        const std::string kind = params.at("kind");
        const long tolerance = static_cast<long>(number("tol", 2));
        std::string failure;
        bool cancelled = false, passed = true;

        try {
            guard();
            const auto limits = host_.driver->motion()->plannerLimits();
            const auto speedOf = [&](int axis) {
                return speedLadder(axis, number("distance", axis == 2 ? 20 : 100),
                                   number("from", std::max(1.0, limits.maxFeed[axis] * 0.2)),
                                   number("to", limits.maxFeed[axis]), static_cast<int>(number("levels", 6)),
                                   static_cast<int>(number("repeats", 3)), tolerance);
            };
            const auto accelOf = [&](int axis) {
                return accelLadder(axis, number("distance", axis == 2 ? 5 : 20),
                                   number("from", limits.maxAcceleration[axis] * 0.25),
                                   number("to", limits.maxAcceleration[axis]), static_cast<int>(number("levels", 6)),
                                   static_cast<int>(number("repeats", 10)), tolerance);
            };
            healthRows([&] {
                if (kind == "repeat") passed = repeatability(axisOf(), static_cast<int>(number("n", 10)));
                else if (kind == "speed") passed = speedOf(axisOf());
                else if (kind == "accel") passed = accelOf(axisOf());
                else if (kind == "travel") passed = travelCheck(axisOf(), number("speed", 20), tolerance);
                else if (kind == "tnoise") passed = sensorNoise(number("seconds", 20));
                else if (kind == "heat") {
                    passed = heatTest(number("target", host_.settings->number("preheat")), number("hold", 60),
                                      number("timeout", 600));
                } else if (kind == "stress") {
                    passed = stress(number("minutes", 10), number("check", 2), params.count("z") && params.at("z") != "0",
                                    static_cast<unsigned>(number("seed", 1)), tolerance, params.count("force") > 0);
                } else {
                    const std::string name = params.at("name");
                    if (name == "mechanics" || name == "all") {
                        for (int axis = 0; axis < 2; ++axis) passed = repeatability(axis, 10) && passed;
                        for (int axis = 0; axis < 2; ++axis) passed = speedOf(axis) && passed;
                        for (int axis = 0; axis < 2; ++axis) passed = accelOf(axis) && passed;
                        passed = stress(number("minutes", 3), number("check", 1), false, 1, tolerance, false) && passed;
                    }
                    if (name == "thermal" || name == "all") {
                        passed = sensorNoise(20) && passed;
                        passed = heatTest(number("target", host_.settings->number("preheat")), 60, 600) && passed;
                    }
                }
            });
        } catch (const Stop &stop) {
            cancelled = stop.cancelled;
            failure = stop.reason;
        } catch (const std::exception &e) {
            failure = e.what();
        }

        // In ogni caso: heater spento, valori della configurazione di nuovo nel driver, scheda in attesa
        if (heating_) host_.driver->temperature()->setHotendTemperature(0);
        heating_ = false;
        host_.restore();
        if (!host_.driver->isLinkFaulted() && host_.driver->getState() != core::PrintState::Error) {
            host_.driver->motion()->flushPlanned();
            host_.driver->waitForIdle(std::chrono::seconds(60));
            const auto state = host_.driver->getState();
            if (state == core::PrintState::Printing || state == core::PrintState::Paused) host_.driver->system()->endPrint();
        }

        std::string state, summary;
        if (cancelled) {
            state = "CANCELLED";
            summary = "annullata dall'operatore";
        } else if (!failure.empty()) {
            state = "FAILED";
            summary = "non eseguita fino in fondo: " + failure;
        } else {
            state = "COMPLETED";
            summary = passed ? "superata" : "non superata: vedere le righe segnate";
        }
        host_.history->addEvent(jobId_, cancelled || failure.empty() ? "info" : "error", "esito: " + summary);
        host_.history->finish(jobId_, state, failure);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            verdict_ = cancelled ? "cancelled" : !failure.empty() ? "error" : passed ? "ok" : "bad";
            summary_ = summary;
            phase_ = "finita";
            progress_ = 1.0;
            finishedAt_ = static_cast<long long>(std::time(nullptr));
        }
        active_ = false;
    }

    std::string Measure::json() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream out;
        out << "{\"ok\":true,\"active\":" << (active_ ? "true" : "false") << ",\"kind\":" << quote(kind_)
            << ",\"title\":" << quote(title_) << ",\"phase\":" << quote(phase_) << ",\"progress\":" << fmt(progress_, 3)
            << ",\"verdict\":" << quote(verdict_) << ",\"summary\":" << quote(summary_)
            << ",\"startedAt\":" << startedAt_ << ",\"finishedAt\":" << finishedAt_ << ",\"rows\":[";
        for (size_t i = 0; i < rows_.size(); ++i) {
            out << (i ? "," : "") << "{\"section\":" << quote(rows_[i].section) << ",\"label\":" << quote(rows_[i].label)
                << ",\"value\":" << quote(rows_[i].value) << ",\"verdict\":" << quote(rows_[i].verdict) << "}";
        }
        out << "],\"log\":[";
        for (size_t i = 0; i < log_.size(); ++i) out << (i ? "," : "") << quote(log_[i]);
        out << "],\"suggest\":{";
        bool first = true;
        for (const auto &[key, value]: suggest_) {
            out << (first ? "" : ",") << quote(key) << ":" << quote(value);
            first = false;
        }
        out << "}}";
        return out.str();
    }

    std::string Measure::stepsAdvice(const Params &params, bool &ok) {
        ok = false;
        const auto text = [&params](const char *key) {
            const auto it = params.find(key);
            return it == params.end() ? std::string() : it->second;
        };
        const std::string axis = text("axis");
        const double commanded = std::atof(text("commanded").c_str()), measured = std::atof(text("measured").c_str());
        const size_t index = axis.empty() ? std::string::npos
                                          : std::string("XYZE").find(static_cast<char>(std::toupper(static_cast<unsigned char>(axis[0]))));
        if (index == std::string::npos) return "steps: axis must be x, y, z or e";
        if (commanded <= 0.0 || measured <= 0.0) return "steps: commanded= and measured= must be positive (millimetres)";
        if (measured < commanded * 0.5 || measured > commanded * 2.0) {
            return "steps: the measured value is more than twice off: check the measurement before changing anything";
        }
        const auto info = host_.driver->motion()->machineInfo();
        if (!info.valid) return "steps: the firmware did not report its steps per millimetre";
        const double current = info.limits.stepsPerMm[index], proposed = current * commanded / measured;
        ok = true;
        const char *names[4] = {"STEPS_PER_MM_X", "STEPS_PER_MM_Y", "STEPS_PER_MM_Z", "STEPS_PER_MM_E"};
        return std::string("asse ") + "XYZE"[index] + ": chiesti " + fmt(commanded, 2) + " mm, misurati " +
               fmt(measured, 2) + " (" + fmt((measured / commanded - 1.0) * 100.0, 2) + "%). Step per millimetro: da " +
               fmt(current, 2) + " a " + fmt(proposed, 2) + ". E' una costante del firmware (" + names[index] +
               (index == 3 ? " in ExtruderConfig.hpp" : " in MotionConfig.hpp") + "): va cambiata li' e la scheda ricaricata.";
    }
}
