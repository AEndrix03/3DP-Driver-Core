/**
 * 3dp - servizio locale e riga di comando.
 *
 *   3dp serve --port COM3 [--http 7333] [--data ./3dp-data]
 *       tiene la porta seriale, esegue le stampe, espone le API su http://127.0.0.1:7333
 *       e serve la pagina di gestione. Nessun backend, Kafka o database esterno.
 *
 *   3dp serve --sim [--speed 12]
 *       come sopra, ma con la stampante simulata (firmware vero compilato per il PC):
 *       per provare pagina e file senza hardware. Richiede `tools/build_local.sh --sim`.
 *
 *   3dp <comando> [chiave=valore ...]
 *       client delle stesse API: status, home, jog, temp, fan, gcode, print, upload,
 *       pause, resume, cancel, estop, clear, history, diag. Stampa la risposta JSON.
 *
 * Una sola API, due clienti (pagina web e CLI): solo il servizio possiede la seriale.
 */
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#endif
#include "core/serial/LinuxBaud.hpp"
#include "../../third_party/httplib.h"

#include "WebUi.hpp"
#include "Slicer.hpp"
#include "SimPort.hpp"
#include "WirePort.hpp"
#include "Settings.hpp"
#include "core/DriverInterface.hpp"
#include "core/printer/job/GCodeDownloader.hpp"
#include "core/printer/job/PrintJobManager.hpp"
#include "core/printer/job/tracking/JobTracker.hpp"
#include "core/printer/state/StateTracker.hpp"
#include "core/queue/CommandExecutorQueue.hpp"
#include "local/History.hpp"
#include "local/NativeSerialPort.hpp"
#include "logger/Logger.hpp"
#include "translator/GCodeTranslator.hpp"
#include "translator/dispatchers/fan/FanDispatcher.hpp"
#include "translator/dispatchers/motion/MotionDispatcher.hpp"
#include "translator/dispatchers/system/SystemDispatcher.hpp"
#include "translator/dispatchers/temperature/TemperatureDispatcher.hpp"
#include "translator/dispatchers/extruder/ExtruderDispatcher.hpp"
#include "translator/dispatchers/endstop/EndstopDispatcher.hpp"
#include "translator/dispatchers/compat/CompatibilityDispatcher.hpp"
#include "translator/GCodeSupport.hpp"
#include "translator/GCodePreview.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

// Il download del G-code via HTTP (libcurl) non serve in locale: i file sono sul disco.
namespace core::print {
    GCodeDownloader::GCodeDownloader() = default;

    GCodeDownloader::~GCodeDownloader() = default;

    void GCodeDownloader::downloadAsync(const std::string &, const std::string &, ProgressCallback,
                                        CompletionCallback completion) {
        if (completion) completion(false, "", "download not available in the local service");
    }

    void GCodeDownloader::cancelDownload() {}
}

namespace {
    using core::print::JobState;
    using Params = std::map<std::string, std::string>;

    // ------------------------------------------------------------------ JSON minimo
    std::string quote(const std::string &text) {
        std::string out = "\"";
        for (const char c: text) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) out += ' ';
                    else out += c;
            }
        }
        return out + "\"";
    }

    std::string number(double value, int decimals = 3) {
        char buffer[40];
        std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
        return buffer;
    }

    std::string reply(bool ok, const std::string &message, const std::string &extra = "") {
        return std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"message\":" + quote(message) +
               (extra.empty() ? "" : "," + extra) + "}";
    }

    std::string reply(const core::types::Result &result, const std::string &extra = "") {
        std::string body = "\"body\":[";
        for (size_t i = 0; i < result.body.size(); ++i) body += (i ? "," : "") + quote(result.body[i]);
        body += "]";
        return reply(result.isSuccess(), result.message, extra.empty() ? body : body + "," + extra);
    }

    const char *jobStateName(JobState state) {
        switch (state) {
            case JobState::RUNNING: return "RUNNING";
            case JobState::PAUSED: return "PAUSED";
            case JobState::COMPLETED: return "COMPLETED";
            case JobState::FAILED: return "FAILED";
            case JobState::CANCELLED: return "CANCELLED";
            case JobState::PRECHECK: return "PRECHECK";
            case JobState::HOMING: return "HOMING";
            case JobState::LOADING: return "LOADING";
            case JobState::HEATING: return "HEATING";
            case JobState::QUEUED: return "QUEUED";
            default: return "CREATED";
        }
    }

    bool isActive(JobState state) {
        return state == JobState::RUNNING || state == JobState::PAUSED || state == JobState::PRECHECK ||
               state == JobState::HOMING || state == JobState::LOADING || state == JobState::HEATING;
    }

    std::optional<double> numberParam(const Params &params, const std::string &key) {
        const auto it = params.find(key);
        if (it == params.end() || it->second.empty()) return std::nullopt;
        try {
            return std::stod(it->second);
        } catch (...) {
            return std::nullopt;
        }
    }

    // ------------------------------------------------------------------ servizio
    class Service {
    public:
        /// @param device porta seriale; vuota = stampante simulata alla velocita' `simSpeed`
        Service(const std::string &device, const std::string &dataDir, double simSpeed)
                : dataDir_(dataDir), history_((std::filesystem::path(dataDir) / "history.sqlite").string()),
                  settings_((std::filesystem::path(dataDir) / "config.ini").string()),
                  simulated_(device.empty()) {
            std::shared_ptr<core::SerialPort> raw;
            if (simulated_) {
                raw = local::openSim(simSpeed);
                if (!raw) portError_ = "this build has no simulator (build it with: tools/build_local.sh --sim)";
            } else {
                const auto native = std::make_shared<local::NativeSerialPort>(device, core::DEFAULT_SERIAL_BAUD);
                portError_ = native->lastError();
                raw = native;
            }
            if (raw) port_ = wire_ = std::make_shared<local::WirePort>(raw);
            if (wire_ && simulated_) wire_->setTimeScale(local::simSpeed());
        }

        bool start(std::string &error) {
            if (!port_ || !port_->isOpen()) {
                error = portError_;
                return false;
            }
            if (!history_.isOpen()) {
                error = "history database: " + history_.lastError();
                return false;
            }
            history_.closeStale("INTERRUPTED"); // stampe rimaste aperte da un'esecuzione precedente

            // L'apertura ha riavviato la scheda: si attende che il firmware sia pronto
            bool ready = false;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            while (!ready && std::chrono::steady_clock::now() < deadline) {
                ready = port_->receiveLine().find("Sistema pronto") != std::string::npos;
            }
            if (!ready) {
                error = "the firmware did not answer on the serial port";
                return false;
            }
            if (simulated_) local::simReady();

            driver_ = std::make_shared<core::DriverInterface>(nullptr, port_);
            translator_ = std::make_shared<translator::gcode::GCodeTranslator>(driver_);
            translator_->registerDispatcher(std::make_unique<translator::gcode::MotionDispatcher>(driver_));
            translator_->registerDispatcher(std::make_unique<translator::gcode::SystemDispatcher>(driver_));
            translator_->registerDispatcher(std::make_unique<translator::gcode::TemperatureDispatcher>(driver_));
            translator_->registerDispatcher(std::make_unique<translator::gcode::FanDispatcher>(driver_));
            translator_->registerDispatcher(std::make_unique<translator::gcode::ExtruderDispatcher>(driver_));
            translator_->registerDispatcher(std::make_unique<translator::gcode::EndstopDispatcher>(driver_));
            // per ultimo: comandi di contorno degli slicer (G4, M400, G21, M73, ...)
            translator_->registerDispatcher(std::make_unique<translator::gcode::CompatibilityDispatcher>(driver_));
            queue_ = std::make_shared<core::CommandExecutorQueue>(translator_);
            manager_ = std::make_shared<core::print::PrintJobManager>(driver_, queue_);

            if (!driver_->system()->printStatus().isSuccess()) {
                error = "the firmware did not accept the first command";
                return false;
            }
            applySettings(true);
            // Limiti, coda e corsa dichiarati dalla scheda: senza non si pianifica. Meglio saperlo subito.
            const auto machine = driver_->motion()->readMachine();
            if (!machine.isSuccess()) {
                error = "the firmware did not report its limits (" + machine.message +
                        "): the board probably runs an older firmware, flash the current one";
                return false;
            }
            running_ = true;
            poller_ = std::thread([this] { pollLoop(); });
            return true;
        }

        void stop() {
            running_ = false;
            if (poller_.joinable()) poller_.join();
            if (queue_) queue_->stop();
        }

        // ---- lettura ----
        std::string status() {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            const JobState state = manager_->getCurrentState();
            const auto info = core::jobs::JobTracker::getInstance().getJobInfo(activeJob_);

            std::ostringstream out;
            out << "{\"mode\":" << quote(simulated_ ? "sim" : activeJob_.empty() ? "idle" : "print")
                << ",\"speedPercent\":" << number(settings_.number("speed_percent"), 0)
                << ",\"blocksPerSecond\":" << number(settings_.number("blocks_per_second"), 0)
                << ",\"preheat\":" << number(settings_.number("preheat"), 0)
                << ",\"connected\":" << (port_->isOpen() && !driver_->isLinkFaulted() ? "true" : "false")
                << ",\"state\":" << quote(core::DriverInterface::printStateToString(driver_->getState()))
                << ",\"fault\":" << quote(faultText_)
                << ",\"lastResult\":" << quote(lastResult_)
                << ",\"queue\":" << queue_->getQueueSize()
                << ",\"job\":{\"id\":" << quote(activeJob_) << ",\"name\":" << quote(activeName_)
                << ",\"state\":" << quote(activeJob_.empty() ? "" : jobStateName(state))
                << ",\"percent\":" << number(info ? info->getProgress() : 0.0, 2)
                << ",\"linesDone\":" << (info ? info->executedCommands : 0)
                << ",\"linesTotal\":" << (info ? info->totalCommands : 0)
                << ",\"test\":" << (activeTest_ ? "true" : "false")
                << ",\"file\":" << quote(activePath_)
                << ",\"elapsed\":" << (activeJob_.empty() ? 0 : static_cast<long long>(std::time(nullptr) - activeStart_))
                << "}"
                << ",\"linearAdvance\":" << driver_->motion()->linearAdvance()
                << ",\"slicer\":{\"found\":" << (slicer_.binaryFound && slicer_.profileFound ? "true" : "false")
                << ",\"path\":" << quote(slicer_.binary) << "}"
                << ",\"sim\":" << (simulated_ ? "{\"speed\":" + number(local::simSpeed(), 2) + "}" : "null")
                << ",\"volume\":{\"x\":" << settings_.number("volume_x") << ",\"y\":" << settings_.number("volume_y")
                << ",\"z\":" << settings_.number("volume_z") << "}"
                << ",\"temperature\":{\"known\":" << (tempKnown_ ? "true" : "false")
                << ",\"current\":" << number(tempCurrent_, 1) << ",\"target\":" << number(tempTarget_, 1) << "}"
                << ",\"position\":{\"known\":" << (posKnown_ ? "true" : "false")
                << ",\"referenced\":" << (posReferenced_ ? "true" : "false")
                << ",\"x\":" << number(posX_) << ",\"y\":" << number(posY_) << ",\"z\":" << number(posZ_) << "}}";
            return out.str();
        }

        std::string history(int limit) {
            std::string out = "{\"ok\":true,\"prints\":[";
            bool first = true;
            for (const auto &r: history_.list(limit)) {
                out += first ? "" : ",";
                first = false;
                out += "{\"id\":" + std::to_string(r.id) + ",\"jobId\":" + quote(r.jobId) + ",\"name\":" + quote(r.name) +
                       ",\"gcodePath\":" + quote(r.gcodePath) + ",\"sourcePath\":" + quote(r.sourcePath) +
                       ",\"slicerParams\":" + quote(r.slicerParams) + ",\"state\":" + quote(r.state) +
                       ",\"error\":" + quote(r.error) + ",\"startedAt\":" + std::to_string(r.startedAt) +
                       ",\"finishedAt\":" + std::to_string(r.finishedAt) + ",\"lines\":" + std::to_string(r.lines) +
                       ",\"events\":[";
                for (size_t i = 0; i < r.events.size(); ++i) {
                    out += (i ? "," : "") + std::string("{\"at\":") + std::to_string(r.events[i].at) + ",\"level\":" +
                           quote(r.events[i].level) + ",\"text\":" + quote(r.events[i].text) + "}";
                }
                out += "]}";
            }
            return out + "]}";
        }

        /**
         * Cio' che la macchina sta facendo davvero, per la pagina: posizione eseguita (quella
         * inviata meno i movimenti che il firmware non ha ancora completato), velocita' del
         * movimento in corso, coda, contatori del collegamento e ultimi scambi sulla seriale.
         */
        std::string live(uint64_t after) {
            const auto wire = wire_->live();
            const auto sent = driver_->motion()->sentPosition();
            const auto limits = driver_->motion()->plannerLimits();
            const bool moving = wire.queued > 0;

            std::ostringstream out;
            out << "{\"ok\":true,\"exec\":{\"known\":" << (sent.valid ? "true" : "false");
            if (sent.valid) {
                const char *names[3] = {"x", "y", "z"};
                for (int a = 0; a < 3; ++a) {
                    out << ",\"" << names[a] << "\":"
                        << number(static_cast<double>(sent.steps[a] - wire.outstanding[a]) / sent.stepsPerMm[a]);
                }
            }
            out << "},\"moving\":" << (moving ? "true" : "false")
                << ",\"speed\":" << number(moving ? wire.speed : 0.0, 2)
                << ",\"acceleration\":" << number(moving ? wire.acceleration : 0.0, 0)
                // inviati e non ancora eseguiti: quelli nella coda della scheda piu' quelli ancora in viaggio
                // strada percorsa dall'inizio del lavoro: dice a che punto del percorso si e', anche
                // quando il percorso ripassa piu' volte sugli stessi punti
                << ",\"travelled\":" << number(wire.travelled - jobTravelled_, 2)
                << ",\"queued\":" << wire.queued << ",\"queueSize\":"
                << limits.queueBlocks + static_cast<int>(core::CommandExecutor::WINDOW_FRAMES)
                << ",\"link\":{\"framesTx\":" << wire.framesTx << ",\"acksRx\":" << wire.acksRx
                << ",\"textTx\":" << wire.textTx << ",\"textRx\":" << wire.textRx
                << ",\"bytesTx\":" << wire.bytesTx << ",\"bytesRx\":" << wire.bytesRx
                << ",\"resent\":" << wire.resent << ",\"errors\":" << wire.errors << "},"
                << wire_->json(after, 60) << "}";
            return out.str();
        }

        /// Percorso e tempi previsti di un file, con i limiti e il linear advance in uso.
        std::string preview(const std::string &file) {
            if (file.empty() || !std::filesystem::exists(file)) return reply(false, "file not found");
            const auto result = translator::gcode::buildPreview(file, driver_->motion()->plannerLimits(),
                                                                driver_->motion()->linearAdvance(),
                                                                settings_.number("speed_percent") / 100.0,
                                                                settings_.number("blocks_per_second"));
            if (!result.readable) return reply(false, "cannot read the file");
            return translator::gcode::previewJson(result);
        }

        // ---- comandi ----
        std::string command(const std::string &name, const Params &params, const std::string &body) {
            std::lock_guard<std::mutex> lock(actionMutex_);
            const bool printing = isActive(manager_->getCurrentState());

            if (name == "estop") {
                // Prima di tutto: fermare i motori e spegnere l'heater, anche a stampa in corso
                event("error", "arresto di emergenza");
                driver_->abortMotion();
                if (printing) manager_->cancelJob();
                const auto stopped = driver_->motion()->emergencyStop();
                driver_->temperature()->setHotendTemperature(0);
                return reply(stopped.isSuccess(), "emergency stop sent");
            }
            if (name == "pause") {
                const bool ok = manager_->pauseJob();
                if (ok) event("info", "pausa chiesta dall'operatore");
                return reply(ok, "pause");
            }
            if (name == "resume") {
                const bool ok = manager_->resumeJob();
                if (ok) event("info", "ripresa");
                return reply(ok, ok ? "resume" : manager_->lastRecoveryError());
            }
            if (name == "cancel") {
                event("warn", "annullata dall'operatore");
                return reply(manager_->cancelJob(), "cancel");
            }
            if (name == "config") return configure(params, printing);
            if (name == "endstops") return reply(driver_->endstop()->readEndstopStatus());
            if (name == "clear") return reply(driver_->system()->clearError());
            if (name == "diag") return reply(driver_->system()->diagnostics());
            if (name == "temp") {
                const auto target = numberParam(params, "s");
                if (!target) return reply(false, "missing s=<degrees>");
                return reply(driver_->temperature()->setHotendTemperature(static_cast<int>(*target)));
            }
            if (name == "fan") {
                const auto percent = numberParam(params, "s");
                if (!percent) return reply(false, "missing s=<percent>");
                return reply(*percent <= 0 ? driver_->fan()->turnOff()
                                           : driver_->fan()->setFanSpeed(static_cast<int>(*percent)));
            }
            if (name == "advance") {
                // Linear advance: vale dai movimenti successivi, anche a stampa in corso
                const auto k = numberParam(params, "k");
                if (!k || *k < 0.0 || *k > 2.0) return reply(false, "missing or invalid k=<0..2 seconds>");
                settings_.set("linear_advance", std::to_string(*k));
                settings_.save();
                driver_->motion()->setLinearAdvance(*k);
                return reply(true, "linear advance set");
            }
            if (name == "simspeed") {
                const auto speed = numberParam(params, "s");
                if (!simulated_) return reply(false, "not a simulated printer");
                if (!speed || *speed <= 0.0) return reply(false, "missing s=<time scale>");
                local::simSetSpeed(*speed);
                wire_->setTimeScale(local::simSpeed());
                return reply(true, "simulation speed set");
            }
            if (name == "upload") return upload(params, body);
            if (name == "check") {
                const auto it = params.find("file");
                if (it == params.end()) return reply(false, "missing file=<path>");
                const auto report = checkFile(it->second);
                return reply(report.printable(), report.printable() ? "printable" : "not printable",
                             "\"report\":" + local::reportJson(report));
            }
            if (name == "slice") return sliceModel(params);
            if (name == "print") return print(params);
            if (name == "gcode") {
                const auto it = params.find("line");
                const std::string line = it != params.end() ? it->second : body;
                if (line.empty()) return reply(false, "missing line=<g-code>");
                if (printing) {
                    queue_->enqueue(line, 1); // urgente: passa davanti alle righe del job
                    return reply(true, "queued ahead of the print");
                }
                try {
                    translator_->parseLine(line);
                    return reply(true, "executed");
                } catch (const std::exception &e) {
                    return reply(false, e.what());
                }
            }

            // Da qui: movimenti manuali, non ammessi durante una stampa
            if (printing) return reply(false, "not allowed while a print is active");

            if (name == "test") return test(params);
            if (name == "home") {
                toIdle();
                const auto it = params.find("axes");
                return reply(driver_->system()->homeAxes(it != params.end() ? it->second : ""));
            }
            if (name == "jog") {
                core::command::motion::MoveTarget target;
                target.x = numberParam(params, "x");
                target.y = numberParam(params, "y");
                target.z = numberParam(params, "z");
                target.e = numberParam(params, "e");
                target.feedrate = numberParam(params, "f");
                if (!target.x && !target.y && !target.z && !target.e) return reply(false, "missing x= y= z= or e=");
                // Il firmware accetta movimenti solo in stato Printing
                if (driver_->getState() == core::PrintState::Idle) {
                    const auto started = driver_->system()->startPrint();
                    if (!started.isSuccess()) return reply(started);
                }
                return reply(driver_->motion()->moveRelative(target));
            }
            return reply(false, "unknown command: " + name);
        }

    private:
        std::string dataDir_;
        local::History history_;
        local::Settings settings_;
        std::shared_ptr<core::SerialPort> port_;
        std::shared_ptr<local::WirePort> wire_; ///< la stessa porta, vista come traccia degli scambi
        std::string portError_;
        bool simulated_ = false;
        std::shared_ptr<core::DriverInterface> driver_;
        std::shared_ptr<translator::gcode::GCodeTranslator> translator_;
        std::shared_ptr<core::CommandExecutorQueue> queue_;
        std::shared_ptr<core::print::PrintJobManager> manager_;

        std::mutex actionMutex_; // un comando dell'operatore alla volta
        std::mutex cacheMutex_;
        std::thread poller_;
        std::atomic<bool> running_{false};

        bool activeTest_ = false;
        local::SlicerSetup slicer_;
        std::atomic<double> jobTravelled_{0.0}; ///< strada gia' percorsa quando il lavoro e' partito
        uint64_t jobResent_ = 0, jobErrors_ = 0, jobFrames_ = 0; ///< contatori del collegamento a inizio lavoro
        std::string activeJob_, activeName_, activePath_, faultText_, lastResult_;
        std::time_t activeStart_ = 0;
        bool tempKnown_ = false, posKnown_ = false, posReferenced_ = false;
        double tempCurrent_ = 0, tempTarget_ = 0, posX_ = 0, posY_ = 0, posZ_ = 0;

        translator::gcode::FileReport checkFile(const std::string &path) {
            return translator::gcode::checkFile(path, settings_.number("volume_x"), settings_.number("volume_y"),
                                                settings_.number("volume_z"));
        }

        /// Annota qualcosa nello storico del lavoro in corso (niente se non ce n'e' uno).
        void event(const std::string &level, const std::string &text) {
            std::string job;
            {
                std::lock_guard<std::mutex> lock(cacheMutex_);
                job = activeJob_;
            }
            if (!job.empty()) history_.addEvent(job, level, text);
        }

        /// Porta nel driver i valori della configurazione. `all` = anche quelli che valgono solo da fermi.
        void applySettings(bool all) {
            const auto motion = driver_->motion();
            motion->setSpeedFactor(settings_.number("speed_percent") / 100.0);
            motion->setBlockRate(settings_.number("blocks_per_second"));
            motion->setAccelerations(settings_.number("accel_print"), settings_.number("accel_travel"));
            motion->setLinearAdvance(settings_.number("linear_advance"));
            for (const char *key: {"slicer_path", "slicer_profile"}) {
                const std::string value = settings_.text(key);
                const char *variable = std::string(key) == "slicer_path" ? "THREEDP_SLICER" : "THREEDP_SLICER_PROFILE";
                if (value.empty()) continue; // vuoto = resta la variabile d'ambiente, se c'e'
#ifdef _WIN32
                _putenv_s(variable, value.c_str());
#else
                setenv(variable, value.c_str(), 1);
#endif
            }
            {
                // dopo aver letto i percorsi della configurazione
                std::lock_guard<std::mutex> lock(cacheMutex_);
                slicer_ = local::locateSlicer();
            }
            if (!all) return;

            core::planning::MachineLimits caps;
            for (int a = 0; a < 4; ++a) caps.maxFeed[a] = caps.maxAcceleration[a] = 0.0;
            caps.junctionDeviation = settings_.number("junction_deviation");
            caps.maxFeed[0] = caps.maxFeed[1] = settings_.number("max_feed_xy");
            caps.maxFeed[2] = settings_.number("max_feed_z");
            caps.maxFeed[3] = settings_.number("max_feed_e");
            caps.maxAcceleration[0] = caps.maxAcceleration[1] = settings_.number("max_accel_xy");
            caps.maxAcceleration[2] = settings_.number("max_accel_z");
            caps.maxAcceleration[3] = settings_.number("max_accel_e");
            motion->setLimitCaps(caps);

            const double p = settings_.number("pid_p"), i = settings_.number("pid_i"), d = settings_.number("pid_d");
            if (p > 0.0 || i > 0.0 || d > 0.0) driver_->temperature()->setPid(p, i, d);
        }

        /// La configurazione, con accanto cio' che il firmware ha dichiarato (sola lettura).
        std::string configJson(bool ok, const std::string &message) {
            const auto info = driver_->motion()->machineInfo();
            std::ostringstream fw;
            fw << "\"firmware\":{\"known\":" << (info.valid ? "true" : "false");
            if (info.valid) {
                const auto &l = info.limits;
                fw << ",\"stepsPerMm\":[" << l.stepsPerMm[0] << "," << l.stepsPerMm[1] << "," << l.stepsPerMm[2] << ","
                   << l.stepsPerMm[3] << "],\"maxFeed\":[" << l.maxFeed[0] << "," << l.maxFeed[1] << "," << l.maxFeed[2]
                   << "," << l.maxFeed[3] << "],\"maxAcceleration\":[" << l.maxAcceleration[0] << ","
                   << l.maxAcceleration[1] << "," << l.maxAcceleration[2] << "," << l.maxAcceleration[3]
                   << "],\"junctionDeviation\":" << l.junctionDeviation << ",\"maxStepRate\":" << l.maxStepRate
                   << ",\"queueBlocks\":" << l.queueBlocks << ",\"receiveBuffer\":" << info.receiveBuffer
                   << ",\"travel\":[" << info.travelMm[0] << "," << info.travelMm[1] << "," << info.travelMm[2] << "]";
            }
            fw << "}";
            return reply(ok, message, settings_.json() + "," + fw.str());
        }

        std::string configure(const Params &params, bool printing) {
            if (params.empty()) return configJson(true, "configuration");
            // prima si controlla tutto, poi si cambia: o passano tutte le voci o nessuna
            for (const auto &[key, value]: params) {
                for (const local::Setting &s: local::Settings::catalog()) {
                    if (key == s.key && printing && !s.whilePrinting) {
                        return configJson(false, std::string(s.label) + ": can be changed only while not printing");
                    }
                }
            }
            local::Settings trial = settings_copy();
            for (const auto &[key, value]: params) {
                const std::string refused = trial.set(key, value);
                if (!refused.empty()) return configJson(false, refused);
            }
            for (const auto &[key, value]: params) settings_.set(key, value);
            if (!settings_.save()) return configJson(false, "cannot write the configuration file");
            applySettings(!printing);
            return configJson(true, "saved");
        }

        /// Copia di prova, su un file che non viene mai scritto.
        local::Settings settings_copy() { return local::Settings(""); }

        /**
         * Utilita' di prova: generano un piccolo G-code (movimenti relativi alla posizione attuale)
         * e lo eseguono come un lavoro qualsiasi, cosi' si vede nel visualizzatore, si puo'
         * mettere in pausa o annullare e resta nello storico con i suoi errori.
         */
        std::string test(const Params &params) {
            const auto text = [&params](const char *key, const std::string &fallback) {
                const auto it = params.find(key);
                return it == params.end() || it->second.empty() ? fallback : it->second;
            };
            const auto value = [&params](const char *key, double fallback) {
                return numberParam(params, key).value_or(fallback);
            };
            const std::string kind = text("kind", "");
            std::string axis = text("axis", "x");
            for (char &c: axis) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            const double distance = value("distance", 50.0), feed = value("feed", 3000.0), feedTo = value("feed2", feed);
            const int repeats = static_cast<int>(value("repeats", 3.0));
            const double segment = value("segment", 0.1);
            if (distance <= 0.0 || distance > 500.0 || feed < 10.0 || feed > 30000.0 || feedTo < 10.0 ||
                feedTo > 30000.0 || repeats < 1 || repeats > 200 || segment < 0.02 || segment > 10.0) {
                return reply(false, "test: value out of range");
            }
            if (axis != "X" && axis != "Y" && axis != "Z") return reply(false, "test: axis must be x, y or z");

            double x, y, z;
            bool known, referenced;
            {
                std::lock_guard<std::mutex> lock(cacheMutex_);
                x = posX_, y = posY_, z = posZ_;
                known = posKnown_, referenced = posReferenced_;
            }
            if (!known) return reply(false, "test: the machine position is not known yet");
            // spazio necessario in X, Y, Z a partire dalla posizione attuale (i test vanno solo in positivo)
            double need[3] = {0, 0, 0};
            std::ostringstream g;
            std::string title;
            const auto num = [](double v) {
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer), "%.3f", v);
                return std::string(buffer);
            };
            const auto show = [](double v) { // per i titoli: senza zeri inutili
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer), "%g", v);
                return std::string(buffer);
            };
            if (kind == "shuttle" || kind == "ladder") {
                need[axis[0] - 'X'] = distance;
                const int steps = kind == "ladder" ? std::max(2, repeats) : repeats;
                for (int i = 0; i < steps; ++i) {
                    const double f = kind == "ladder" ? feed + (feedTo - feed) * i / (steps - 1) : feed;
                    g << "G1 " << axis << num(distance) << " F" << num(f) << "\nG1 " << axis << num(-distance) << "\n";
                }
                title = kind == "ladder"
                        ? "scala di velocita' " + axis + " " + show(distance) + " mm, " + show(feed / 60) + "-" + show(feedTo / 60) + " mm/s"
                        : "va e vieni " + axis + " " + show(distance) + " mm a " + show(feed / 60) + " mm/s x" + std::to_string(repeats);
            } else if (kind == "square") {
                need[0] = need[1] = distance;
                for (int i = 0; i < repeats; ++i) {
                    g << "G1 X" << num(distance) << " F" << num(feed) << "\nG1 Y" << num(distance) << "\nG1 X"
                      << num(-distance) << "\nG1 Y" << num(-distance) << "\n";
                }
                title = "quadrato " + show(distance) + " mm a " + show(feed / 60) + " mm/s x" + std::to_string(repeats);
            } else if (kind == "circle") {
                need[0] = need[1] = distance;
                const double r = distance / 2.0;
                g << "G1 Y" << num(r) << " F" << num(feed) << "\n";
                for (int i = 0; i < repeats; ++i) g << "G2 I" << num(r) << " J0 F" << num(feed) << "\n";
                g << "G1 Y" << num(-r) << "\n";
                title = "cerchio diametro " + show(distance) + " mm a " + show(feed / 60) + " mm/s x" + std::to_string(repeats);
            } else if (kind == "stream") {
                need[axis[0] - 'X'] = distance;
                const int count = static_cast<int>(distance / segment);
                if (count < 1 || count * repeats > 400000) return reply(false, "test: too many segments");
                for (int i = 0; i < repeats; ++i) {
                    for (int k = 0; k < count; ++k) g << "G1 " << axis << num(segment) << (k == 0 ? " F" + num(feed) : "") << "\n";
                    for (int k = 0; k < count; ++k) g << "G1 " << axis << num(-segment) << "\n";
                }
                title = "flusso di segmenti da " + show(segment) + " mm su " + axis + " (" + std::to_string(count * 2 * repeats) + " movimenti)";
            } else if (kind == "extrude") {
                g << "G1 E" << num(distance) << " F" << num(std::min(feed, 600.0)) << "\n";
                title = "estrusione " + show(distance) + " mm";
            } else {
                return reply(false, "test: unknown kind (shuttle, ladder, square, circle, stream, extrude)");
            }

            const double room[3] = {settings_.number("volume_x") - x, settings_.number("volume_y") - y,
                                    settings_.number("volume_z") - z};
            const char *names = "XYZ";
            const double whole[3] = {settings_.number("volume_x"), settings_.number("volume_y"),
                                     settings_.number("volume_z")};
            for (int a = 0; a < 3; ++a) {
                if (need[a] > whole[a] + 0.001) {
                    return reply(false, std::string("test: longer than the whole travel of ") + names[a]);
                }
            }
            // Senza homing il firmware non conosce i limiti di corsa: serve un consenso esplicito
            if (!referenced && params.find("force") == params.end()) {
                return reply(false, "test: the position is not referenced (home first): the travel limits cannot be "
                                    "checked. Add force=1 to run it anyway.", "\"needsForce\":true");
            }
            for (int a = 0; a < 3; ++a) {
                if (referenced && need[a] > room[a] + 0.001) {
                    return reply(false, std::string("test: not enough travel on ") + names[a] + " (needs " +
                                        num(need[a]) + " mm, " + num(std::max(0.0, room[a])) + " mm left from here)");
                }
            }

            std::filesystem::create_directories(std::filesystem::path(dataDir_) / "tests");
            const auto path = std::filesystem::path(dataDir_) / "tests" /
                              (kind + "_" + std::to_string(std::time(nullptr)) + ".gcode");
            {
                std::ofstream out(path);
                // ";START" dice all'anteprima da dove parte il percorso (i movimenti sono relativi)
                out << ";START X" << num(x) << " Y" << num(y) << " Z" << num(z) << "\n; Test: " << title
                    << "\nG91\n" << g.str() << "G90\n";
                if (!out.good()) return reply(false, "cannot write " + path.string());
            }
            Params job;
            job["file"] = path.generic_string();
            job["name"] = "Test: " + title;
            job["force"] = "1"; // movimenti relativi: il controllo del volume e' quello fatto qui sopra
            const std::string started = print(job);
            if (!referenced && started.find("\"ok\":true") != std::string::npos) {
                event("warn", "posizione non riferita: i limiti di corsa non sono stati controllati");
            }
            return started;
        }

        /// Dopo i movimenti manuali il firmware resta in Printing: homing e stampe partono da Idle.
        void toIdle() {
            if (driver_->getState() == core::PrintState::Printing || driver_->getState() == core::PrintState::Paused) {
                driver_->waitForIdle(std::chrono::seconds(60));
                driver_->system()->endPrint();
            }
        }

        std::string upload(const Params &params, const std::string &body) {
            if (body.empty()) return reply(false, "empty file");
            const auto it = params.find("name");
            std::string name = it != params.end() ? std::filesystem::path(it->second).filename().string() : "print.gcode";
            for (char &c: name) {
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_')) c = '_';
            }
            const auto path = std::filesystem::path(dataDir_) / "gcode" /
                              (std::to_string(std::time(nullptr)) + "_" + name);
            std::ofstream out(path, std::ios::binary);
            out << body;
            if (!out.good()) return reply(false, "cannot write " + path.string());
            return reply(true, "saved", "\"path\":" + quote(path.generic_string()));
        }

        std::string sliceModel(const Params &params) {
            const auto it = params.find("file");
            if (it == params.end()) return reply(false, "missing file=<model.stl>");
            Params options = params;
            options.erase("file");
            const auto output = std::filesystem::path(dataDir_) / "gcode" /
                                (std::filesystem::path(it->second).stem().string() + "_" +
                                 std::to_string(std::time(nullptr)) + ".gcode");
            const auto sliced = local::slice(it->second, output.generic_string(), options);
            if (!sliced.ok) return reply(false, sliced.message, "\"log\":" + quote(sliced.log));
            const auto report = checkFile(sliced.gcodePath);
            return reply(report.printable(), report.printable() ? "sliced" : "sliced, but not printable",
                         "\"path\":" + quote(sliced.gcodePath) + ",\"source\":" + quote(it->second) +
                         ",\"slicer\":" + quote(sliced.parameters) + ",\"warning\":" + quote(sliced.warning) +
                         ",\"report\":" + local::reportJson(report));
        }

        /**
         * Prova a secco: copia del file senza i comandi che scaldano l'ugello e, se `keepExtruder`
         * e' falso, senza estrusione. Restano tutti i movimenti, con le stesse velocita': serve a
         * provare meccanica e motori prima che ci sia un hotend (o senza consumare filamento).
         * @return percorso della copia, "" se non si riesce a scriverla
         */
        std::string dryCopy(const std::string &source, bool keepExtruder) {
            std::ifstream in(source);
            const auto target = std::filesystem::path(dataDir_) / "gcode" /
                                (std::filesystem::path(source).stem().string() + (keepExtruder ? ".secco-e" : ".secco") +
                                 ".gcode");
            std::ofstream out(target);
            if (!in.is_open() || !out.is_open()) return "";
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const std::string command = translator::gcode::commandOf(line);
                if (command == "M104" || command == "M109" || command == "M140" || command == "M190") {
                    out << "; a secco: " << line << "\n"; // resta come commento: le righe del file non cambiano numero
                    continue;
                }
                const bool move = command == "G0" || command == "G1" || command == "G2" || command == "G3" ||
                                  command == "G5";
                if (keepExtruder || !move) {
                    out << line << "\n";
                    continue;
                }
                // via il parametro E: il movimento resta identico, l'estrusore sta fermo
                const size_t comment = line.find(';');
                std::istringstream tokens(line.substr(0, comment));
                std::string token, kept;
                int parameters = 0;
                while (tokens >> token) {
                    if (kept.empty()) {
                        kept = token; // il comando
                    } else if (token[0] != 'E' && token[0] != 'e') {
                        kept += " " + token;
                        ++parameters;
                    }
                }
                if (parameters == 0) out << "; a secco: " << line << "\n"; // era solo estrusione (ritrazione, spurgo)
                else out << kept << "\n";
            }
            return out.good() ? target.generic_string() : "";
        }

        std::string print(const Params &params) {
            const auto it = params.find("file");
            if (it == params.end() || !std::filesystem::exists(it->second)) return reply(false, "file not found");
            if (isActive(manager_->getCurrentState())) return reply(false, "a print is already active");

            // Un file che uscirebbe dal volume o usa comandi incompatibili non parte (force=1 per insistere)
            const auto report = checkFile(it->second);
            if (!report.printable() && params.find("force") == params.end()) {
                return reply(false, "the file did not pass the check (add force=1 to print anyway)",
                             "\"report\":" + local::reportJson(report));
            }
            // dry=heat: non scalda, l'estrusore gira; dry=motion: non scalda e l'estrusore sta fermo
            std::string runPath = it->second, dryNote;
            if (const auto dry = params.find("dry"); dry != params.end() && !dry->second.empty() && dry->second != "0") {
                if (dry->second != "heat" && dry->second != "motion") return reply(false, "dry must be heat or motion");
                runPath = dryCopy(it->second, dry->second == "heat");
                if (runPath.empty()) return reply(false, "cannot write the dry-run copy of the file");
                dryNote = dry->second == "heat" ? " (a secco)" : " (a secco, solo assi)";
            }
            toIdle();

            local::PrintRecord record;
            record.jobId = "local-" + std::to_string(std::time(nullptr));
            const auto name = params.find("name");
            record.name = (name != params.end() ? name->second : std::filesystem::path(it->second).filename().string()) +
                          dryNote;
            record.gcodePath = runPath;
            const auto source = params.find("source");
            if (source != params.end()) record.sourcePath = source->second;
            const auto slicer = params.find("slicer");
            if (slicer != params.end()) record.slicerParams = slicer->second;
            record.state = "RUNNING";
            {
                std::ifstream file(runPath);
                std::string line;
                while (std::getline(file, line)) ++record.lines;
            }

            if (!manager_->startPrintJob(runPath, record.jobId)) return reply(false, "the print could not start");
            history_.add(record);
            if (!dryNote.empty()) {
                history_.addEvent(record.jobId, "info", dryNote == " (a secco)"
                                                        ? "prova a secco: riscaldamento tolto, estrusore in movimento"
                                                        : "prova a secco: riscaldamento ed estrusione tolti, solo X Y Z");
            }
            const bool isTest = record.name.rfind("Test: ", 0) == 0;
            if (!isTest) {
                for (const auto &warning: report.warnings) history_.addEvent(record.jobId, "info", warning);
            }
            const auto wire = wire_->live();
            wire_->takeErrors(); // quelli di prima non riguardano questo lavoro
            {
                std::lock_guard<std::mutex> lock(cacheMutex_);
                activeTest_ = isTest;
                jobResent_ = wire.resent;
                jobErrors_ = wire.errors;
                jobFrames_ = wire.framesTx;
                jobTravelled_ = wire.travelled;
                activeJob_ = record.jobId;
                activeName_ = record.name;
                activePath_ = std::filesystem::path(runPath).generic_string();
                activeStart_ = std::time(nullptr);
            }
            return reply(true, "print started", "\"jobId\":" + quote(record.jobId));
        }

        /// Aggiorna temperatura, posizione, guasti e chiude nello storico le stampe terminate.
        void pollLoop() {
            uint64_t seenFault = driver_->faultEpoch();
            while (running_) {
                const JobState state = manager_->getCurrentState();
                const bool printing = isActive(state);

                // --- esito della stampa ---
                std::string job;
                {
                    std::lock_guard<std::mutex> lock(cacheMutex_);
                    job = activeJob_;
                }
                // cio' che la scheda ha rifiutato o segnalato, e le ritrasmissioni, finiscono nello storico
                if (!job.empty()) {
                    int kept = 0;
                    for (const std::string &line: wire_->takeErrors()) {
                        if (++kept <= 20) history_.addEvent(job, "warn", "dal firmware: " + line);
                    }
                    const auto wire = wire_->live();
                    if (wire.resent > jobResent_) {
                        history_.addEvent(job, "warn", std::to_string(wire.resent - jobResent_) + " movimenti ritrasmessi");
                        jobResent_ = wire.resent;
                    }
                    if (driver_->faultEpoch() != seenFault) {
                        history_.addEvent(job, "error", "guasto: " + driver_->lastFaultReason());
                    }
                }
                if (!job.empty() && !printing &&
                    (state == JobState::COMPLETED || state == JobState::FAILED || state == JobState::CANCELLED)) {
                    const auto wire = wire_->live();
                    history_.addEvent(job, "info", "collegamento: " + std::to_string(wire.framesTx - jobFrames_) +
                                                   " movimenti inviati, " + std::to_string(wire.errors - jobErrors_) +
                                                   " risposte di errore");
                    const auto info = core::jobs::JobTracker::getInstance().getJobInfo(job);
                    // Il motivo vero e' quello del guasto che ha fermato il job, non il generico "Job failed"
                    std::string error = info ? info->error : "";
                    if (state == JobState::FAILED && (error.empty() || error == "Job failed")) {
                        error = driver_->lastFaultReason();
                    }
                    history_.finish(job, jobStateName(state), error);
                    std::lock_guard<std::mutex> lock(cacheMutex_);
                    lastResult_ = activeName_ + ": " + jobStateName(state) + (error.empty() ? "" : " - " + error);
                    activeJob_.clear();
                    activeName_.clear();
                    activePath_.clear();
                }

                // --- temperatura: un comando ogni 2 s, anche in stampa ---
                double current = 0, target = 0;
                bool known = false;
                const auto temperature = driver_->temperature()->getHotendTemperature();
                for (const auto &line: temperature.body) {
                    if (line.rfind("T:", 0) == 0) {
                        current = std::atof(line.c_str() + 2);
                        known = true;
                    } else if (line.rfind("TARGET:", 0) == 0) {
                        target = std::atof(line.c_str() + 7);
                    }
                }

                // --- posizione: dal firmware da fermi, dal traduttore in stampa (niente traffico in piu') ---
                bool posKnown = false, referenced = false;
                double x = 0, y = 0, z = 0;
                if (printing) {
                    const auto motion = core::state::StateTracker::getInstance().getMotionSnapshot();
                    posKnown = referenced = motion.positionKnown;
                    x = motion.x;
                    y = motion.y;
                    z = motion.z;
                } else if (const auto position = driver_->motion()->getPosition(&referenced)) {
                    posKnown = true;
                    x = position->x;
                    y = position->y;
                    z = position->z;
                }

                {
                    std::lock_guard<std::mutex> lock(cacheMutex_);
                    tempKnown_ = known && current > 0.0;
                    tempCurrent_ = current;
                    tempTarget_ = target;
                    posKnown_ = posKnown;
                    posReferenced_ = referenced;
                    posX_ = x;
                    posY_ = y;
                    posZ_ = z;
                    if (driver_->faultEpoch() != seenFault) {
                        seenFault = driver_->faultEpoch();
                        faultText_ = driver_->lastFaultReason();
                    } else if (driver_->getState() != core::PrintState::Error && !driver_->isLinkFaulted()) {
                        faultText_.clear();
                    }
                }

                for (int i = 0; i < 20 && running_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    };

    // ------------------------------------------------------------------ server HTTP
    int serve(const std::string &device, int httpPort, const std::string &dataDir, double simSpeed) {
        std::filesystem::create_directories(std::filesystem::path(dataDir) / "gcode");
        Logger::init();
        Service service(device, dataDir, simSpeed);
        std::string error;
        if (!service.start(error)) {
            std::cerr << "3dp: " << error << std::endl;
            service.stop();
            Logger::shutdown();
            return 1;
        }

        httplib::Server server;
        server.Get("/", [](const httplib::Request &, httplib::Response &res) {
            res.set_content(local::WEB_UI, "text/html; charset=utf-8");
        });
        server.Get("/api/status", [&](const httplib::Request &, httplib::Response &res) {
            res.set_content(service.status(), "application/json");
        });
        server.Get("/api/config", [&](const httplib::Request &, httplib::Response &res) {
            res.set_content(service.command("config", {}, ""), "application/json");
        });
        server.Get("/api/live", [&](const httplib::Request &req, httplib::Response &res) {
            const uint64_t after = req.has_param("after") ? std::strtoull(req.get_param_value("after").c_str(), nullptr, 10) : 0;
            res.set_content(service.live(after), "application/json");
        });
        server.Get("/api/preview", [&](const httplib::Request &req, httplib::Response &res) {
            res.set_content(service.preview(req.get_param_value("file")), "application/json");
        });
        server.Get("/api/history", [&](const httplib::Request &req, httplib::Response &res) {
            const int limit = req.has_param("limit") ? std::atoi(req.get_param_value("limit").c_str()) : 50;
            res.set_content(service.history(limit > 0 ? limit : 50), "application/json");
        });
        server.Post(R"(/api/([a-z]+))", [&](const httplib::Request &req, httplib::Response &res) {
            Params params;
            for (const auto &entry: req.params) params[entry.first] = entry.second;
            res.set_content(service.command(req.matches[1], params, req.body), "application/json");
        });
        server.set_payload_max_length(512 * 1024 * 1024); // i G-code grandi arrivano dalla pagina

        const auto slicer = local::locateSlicer();
        std::cout << "3dp: slicer " << (slicer.binaryFound ? slicer.binary : "NOT FOUND (models cannot be sliced)")
                  << (slicer.profileFound ? "" : ", machine profile NOT FOUND") << std::endl;
        std::cout << "3dp: " << (device.empty() ? "SIMULATED printer" : "printer on " + device) << ", page and API on http://127.0.0.1:" << httpPort << std::endl;
        // Solo localhost: l'API muove una macchina e non ha autenticazione
        const bool ok = server.listen("127.0.0.1", httpPort);
        service.stop();
        Logger::shutdown();
        if (!ok) {
            std::cerr << "3dp: cannot listen on port " << httpPort << std::endl;
            return 1;
        }
        return 0;
    }

    // ------------------------------------------------------------------ client CLI
    int client(int httpPort, const std::string &name, const std::vector<std::string> &arguments) {
        httplib::Client http("127.0.0.1", httpPort);
        http.set_read_timeout(600, 0); // homing e movimenti lunghi rispondono a operazione finita
        http.set_connection_timeout(3, 0);

        httplib::Params params;
        std::string body;
        for (const auto &argument: arguments) {
            const size_t eq = argument.find('=');
            if (eq != std::string::npos) params.emplace(argument.substr(0, eq), argument.substr(eq + 1));
            else if (name == "print" || name == "upload" || name == "check" || name == "slice") {
                params.emplace("file", argument);
            }
            else if (name == "gcode") params.emplace("line", argument);
            else if (name == "home") params.emplace("axes", argument);
            else if (name == "temp" || name == "fan") params.emplace("s", argument);
            else if (name == "advance") params.emplace("k", argument);
        }

        httplib::Result result{nullptr, httplib::Error::Unknown};
        if (name == "status" || name == "history") {
            result = http.Get(("/api/" + name).c_str());
        } else {
            if (name == "upload") {
                const auto file = params.find("file");
                if (file == params.end()) {
                    std::cerr << "usage: 3dp upload <file.gcode>" << std::endl;
                    return 2;
                }
                std::ifstream in(file->second, std::ios::binary);
                std::stringstream content;
                content << in.rdbuf();
                body = content.str();
                params.emplace("name", std::filesystem::path(file->second).filename().string());
                params.erase("file");
            }
            const std::string path = httplib::append_query_params("/api/" + name, params);
            result = http.Post(path.c_str(), body, "text/plain");
        }

        if (!result) {
            std::cerr << "3dp: service not reachable on port " << httpPort << " (start it with: 3dp serve --port <device>)"
                      << std::endl;
            return 3;
        }
        std::cout << result->body << std::endl;
        return result->body.find("\"ok\":false") != std::string::npos ? 1 : 0;
    }

    void usage() {
        std::cout <<
                  "3dp - local 3D printer service\n\n"
                  "  3dp serve --port <device> [--http 7333] [--data ./3dp-data]\n"
                  "  3dp serve --sim [--speed 12]   simulated printer (firmware built for the PC), no hardware\n"
                  "  3dp status | history | diag\n"
                  "  3dp home [XYZ]            3dp jog x=10 y=-5 z=1 e=2 f=3000\n"
                  "  3dp temp <degrees>        3dp fan <percent>\n"
                  "  3dp advance <K>           linear advance in seconds, 0 = off (same as M900 K)\n"
                  "  3dp config [key=value ...]  show or change the configuration (3dp-data/config.ini)\n"
                  "  3dp test kind=shuttle|ladder|square|circle|stream|extrude [axis=x distance=50 feed=3000\n"
                  "                            feed2=4800 repeats=3 segment=0.1]   motion tests, run as jobs\n"
                  "  3dp endstops              endstop switches, read now\n"
                  "  3dp gcode \"G1 X10 F3000\"\n"
                  "  3dp upload <file.gcode>   3dp print <file.gcode> [name=...] [force=1]\n"
                  "                            [dry=heat|motion]  dry run: no heating; motion = extruder still too\n"
                  "  3dp check <file.gcode>    what the driver would do with the file (no printer needed)\n"
                  "  3dp slice <model.stl> [out=...] [layer=0.2 infill=20 temp=205 perimeters=2 speed=45\n"
                  "                                   supports=0 brim=0]   (PrusaSlicer, no printer needed)\n"
                  "  3dp pause | resume | cancel | clear | estop\n\n"
                  "Client commands accept --http <port> and print the JSON answer of the service.\n";
    }
}

int main(int argc, char **argv) {
    std::vector<std::string> arguments(argv + 1, argv + argc);
    std::string device, dataDir = "3dp-data";
    int httpPort = 7333;
    bool simulated = false;
    double simSpeed = 1.0;

    std::vector<std::string> rest;
    for (size_t i = 0; i < arguments.size(); ++i) {
        const std::string &a = arguments[i];
        if (a == "--port" && i + 1 < arguments.size()) device = arguments[++i];
        else if (a == "--http" && i + 1 < arguments.size()) httpPort = std::atoi(arguments[++i].c_str());
        else if (a == "--data" && i + 1 < arguments.size()) dataDir = arguments[++i];
        else if (a == "--sim") simulated = true;
        else if (a == "--speed" && i + 1 < arguments.size()) simSpeed = std::atof(arguments[++i].c_str());
        else rest.push_back(a);
    }

    if (rest.empty() || rest[0] == "help" || rest[0] == "--help") {
        usage();
        return rest.empty() ? 2 : 0;
    }
    const std::vector<std::string> commandArguments(rest.begin() + 1, rest.end());
    if (rest[0] == "check") {
        // Non serve la stampante: legge il file e dice cosa ne farebbe il driver
        if (commandArguments.empty()) {
            std::cerr << "usage: 3dp check <file.gcode>" << std::endl;
            return 2;
        }
        const auto report = translator::gcode::checkFile(commandArguments[0], local::VOLUME_X, local::VOLUME_Y,
                                                         local::VOLUME_Z);
        const auto estimate = translator::gcode::buildPreview(commandArguments[0], core::planning::MachineLimits{}, 0.0);
        std::cout << "{\"report\":" << local::reportJson(report) << ",\"estimatedSeconds\":"
                  << number(estimate.seconds(), 0) << ",\"heatWaits\":" << estimate.heatWaits << "}" << std::endl;
        return report.printable() ? 0 : 1;
    }
    if (rest[0] == "slice") {
        if (commandArguments.empty()) {
            std::cerr << "usage: 3dp slice <model.stl> [out=file.gcode] [layer=0.2 infill=20 temp=205 "
                         "perimeters=2 speed=45 supports=0 brim=0]" << std::endl;
            return 2;
        }
        std::map<std::string, std::string> options;
        std::string output = std::filesystem::path(commandArguments[0]).replace_extension(".gcode").string();
        for (size_t i = 1; i < commandArguments.size(); ++i) {
            const size_t eq = commandArguments[i].find('=');
            if (eq == std::string::npos) continue;
            const std::string key = commandArguments[i].substr(0, eq), value = commandArguments[i].substr(eq + 1);
            if (key == "out") output = value;
            else options[key] = value;
        }
        const auto sliced = local::slice(commandArguments[0], output, options);
        if (!sliced.ok) {
            std::cerr << "3dp slice: " << sliced.message << "\n" << sliced.log << std::endl;
            return 1;
        }
        const auto report = translator::gcode::checkFile(output, local::VOLUME_X, local::VOLUME_Y, local::VOLUME_Z);
        std::cout << "{\"ok\":" << (report.printable() ? "true" : "false") << ",\"path\":"
                  << quote(std::filesystem::path(output).generic_string())
                  << ",\"report\":" << local::reportJson(report) << "}" << std::endl;
        return report.printable() ? 0 : 1;
    }
    if (rest[0] == "serve") {
        if (simulated) {
            if (!local::simAvailable()) {
                std::cerr << "3dp serve: this build has no simulator (build it with: tools/build_local.sh --sim)"
                          << std::endl;
                return 2;
            }
            // dati separati da quelli della macchina vera
            return serve("", httpPort, dataDir == "3dp-data" ? "3dp-data-sim" : dataDir, simSpeed > 0 ? simSpeed : 1.0);
        }
        if (device.empty()) {
            std::cerr << "3dp serve: missing --port <device> (e.g. COM3 or /dev/ttyUSB0), or --sim" << std::endl;
            return 2;
        }
        return serve(device, httpPort, dataDir, 1.0);
    }
    return client(httpPort, rest[0], std::vector<std::string>(rest.begin() + 1, rest.end()));
}
