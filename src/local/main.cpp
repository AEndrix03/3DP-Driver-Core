/**
 * 3dp - servizio locale e riga di comando.
 *
 *   3dp serve --port COM3 [--http 7333] [--data ./3dp-data]
 *       tiene la porta seriale, esegue le stampe, espone le API su http://127.0.0.1:7333
 *       e serve la pagina di gestione. Nessun backend, Kafka o database esterno.
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
#include "../../third_party/httplib.h"

#include "WebUi.hpp"
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

#include <atomic>
#include <chrono>
#include <cstdio>
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
        Service(const std::string &device, const std::string &dataDir)
                : dataDir_(dataDir), history_((std::filesystem::path(dataDir) / "history.sqlite").string()) {
            port_ = std::make_shared<local::NativeSerialPort>(device, 115200);
        }

        bool start(std::string &error) {
            if (!port_->isOpen()) {
                error = port_->lastError();
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

            driver_ = std::make_shared<core::DriverInterface>(nullptr, port_);
            translator_ = std::make_shared<translator::gcode::GCodeTranslator>(driver_);
            translator_->registerDispatcher(std::make_unique<translator::gcode::MotionDispatcher>(driver_));
            translator_->registerDispatcher(std::make_unique<translator::gcode::SystemDispatcher>(driver_));
            translator_->registerDispatcher(std::make_unique<translator::gcode::TemperatureDispatcher>(driver_));
            translator_->registerDispatcher(std::make_unique<translator::gcode::FanDispatcher>(driver_));
            queue_ = std::make_shared<core::CommandExecutorQueue>(translator_);
            manager_ = std::make_shared<core::print::PrintJobManager>(driver_, queue_);

            if (!driver_->system()->printStatus().isSuccess()) {
                error = "the firmware did not accept the first command";
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
            out << "{\"connected\":" << (port_->isOpen() && !driver_->isLinkFaulted() ? "true" : "false")
                << ",\"state\":" << quote(core::DriverInterface::printStateToString(driver_->getState()))
                << ",\"fault\":" << quote(faultText_)
                << ",\"queue\":" << queue_->getQueueSize()
                << ",\"job\":{\"id\":" << quote(activeJob_) << ",\"name\":" << quote(activeName_)
                << ",\"state\":" << quote(activeJob_.empty() ? "" : jobStateName(state))
                << ",\"percent\":" << number(info ? info->getProgress() : 0.0, 2)
                << ",\"linesDone\":" << (info ? info->executedCommands : 0)
                << ",\"linesTotal\":" << (info ? info->totalCommands : 0) << "}"
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
                       ",\"finishedAt\":" + std::to_string(r.finishedAt) + ",\"lines\":" + std::to_string(r.lines) + "}";
            }
            return out + "]}";
        }

        // ---- comandi ----
        std::string command(const std::string &name, const Params &params, const std::string &body) {
            std::lock_guard<std::mutex> lock(actionMutex_);
            const bool printing = isActive(manager_->getCurrentState());

            if (name == "estop") {
                // Prima di tutto: fermare i motori e spegnere l'heater, anche a stampa in corso
                driver_->abortMotion();
                if (printing) manager_->cancelJob();
                const auto stopped = driver_->motion()->emergencyStop();
                driver_->temperature()->setHotendTemperature(0);
                return reply(stopped.isSuccess(), "emergency stop sent");
            }
            if (name == "pause") return reply(manager_->pauseJob(), "pause");
            if (name == "resume") {
                const bool ok = manager_->resumeJob();
                return reply(ok, ok ? "resume" : manager_->lastRecoveryError());
            }
            if (name == "cancel") return reply(manager_->cancelJob(), "cancel");
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
            if (name == "upload") return upload(params, body);
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
        std::shared_ptr<local::NativeSerialPort> port_;
        std::shared_ptr<core::DriverInterface> driver_;
        std::shared_ptr<translator::gcode::GCodeTranslator> translator_;
        std::shared_ptr<core::CommandExecutorQueue> queue_;
        std::shared_ptr<core::print::PrintJobManager> manager_;

        std::mutex actionMutex_; // un comando dell'operatore alla volta
        std::mutex cacheMutex_;
        std::thread poller_;
        std::atomic<bool> running_{false};

        std::string activeJob_, activeName_, faultText_;
        bool tempKnown_ = false, posKnown_ = false, posReferenced_ = false;
        double tempCurrent_ = 0, tempTarget_ = 0, posX_ = 0, posY_ = 0, posZ_ = 0;

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

        std::string print(const Params &params) {
            const auto it = params.find("file");
            if (it == params.end() || !std::filesystem::exists(it->second)) return reply(false, "file not found");
            if (isActive(manager_->getCurrentState())) return reply(false, "a print is already active");
            toIdle();

            local::PrintRecord record;
            record.jobId = "local-" + std::to_string(std::time(nullptr));
            const auto name = params.find("name");
            record.name = name != params.end() ? name->second : std::filesystem::path(it->second).filename().string();
            record.gcodePath = it->second;
            const auto source = params.find("source");
            if (source != params.end()) record.sourcePath = source->second;
            const auto slicer = params.find("slicer");
            if (slicer != params.end()) record.slicerParams = slicer->second;
            record.state = "RUNNING";
            {
                std::ifstream file(it->second);
                std::string line;
                while (std::getline(file, line)) ++record.lines;
            }

            if (!manager_->startPrintJob(it->second, record.jobId)) return reply(false, "the print could not start");
            history_.add(record);
            {
                std::lock_guard<std::mutex> lock(cacheMutex_);
                activeJob_ = record.jobId;
                activeName_ = record.name;
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
                if (!job.empty() && !printing &&
                    (state == JobState::COMPLETED || state == JobState::FAILED || state == JobState::CANCELLED)) {
                    const auto info = core::jobs::JobTracker::getInstance().getJobInfo(job);
                    history_.finish(job, jobStateName(state), info ? info->error : "");
                    std::lock_guard<std::mutex> lock(cacheMutex_);
                    activeJob_.clear();
                    activeName_.clear();
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
    int serve(const std::string &device, int httpPort, const std::string &dataDir) {
        std::filesystem::create_directories(std::filesystem::path(dataDir) / "gcode");
        Logger::init();
        Service service(device, dataDir);
        std::string error;
        if (!service.start(error)) {
            std::cerr << "3dp: " << error << std::endl;
            return 1;
        }

        httplib::Server server;
        server.Get("/", [](const httplib::Request &, httplib::Response &res) {
            res.set_content(local::WEB_UI, "text/html; charset=utf-8");
        });
        server.Get("/api/status", [&](const httplib::Request &, httplib::Response &res) {
            res.set_content(service.status(), "application/json");
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

        std::cout << "3dp: printer on " << device << ", page and API on http://127.0.0.1:" << httpPort << std::endl;
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
            else if (name == "print" || name == "upload") params.emplace("file", argument);
            else if (name == "gcode") params.emplace("line", argument);
            else if (name == "home") params.emplace("axes", argument);
            else if (name == "temp" || name == "fan") params.emplace("s", argument);
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
                  "  3dp status | history | diag\n"
                  "  3dp home [XYZ]            3dp jog x=10 y=-5 z=1 e=2 f=3000\n"
                  "  3dp temp <degrees>        3dp fan <percent>\n"
                  "  3dp gcode \"G1 X10 F3000\"\n"
                  "  3dp upload <file.gcode>   3dp print <file.gcode> [name=...]\n"
                  "  3dp pause | resume | cancel | clear | estop\n\n"
                  "Client commands accept --http <port> and print the JSON answer of the service.\n";
    }
}

int main(int argc, char **argv) {
    std::vector<std::string> arguments(argv + 1, argv + argc);
    std::string device, dataDir = "3dp-data";
    int httpPort = 7333;

    std::vector<std::string> rest;
    for (size_t i = 0; i < arguments.size(); ++i) {
        const std::string &a = arguments[i];
        if (a == "--port" && i + 1 < arguments.size()) device = arguments[++i];
        else if (a == "--http" && i + 1 < arguments.size()) httpPort = std::atoi(arguments[++i].c_str());
        else if (a == "--data" && i + 1 < arguments.size()) dataDir = arguments[++i];
        else rest.push_back(a);
    }

    if (rest.empty() || rest[0] == "help" || rest[0] == "--help") {
        usage();
        return rest.empty() ? 2 : 0;
    }
    if (rest[0] == "serve") {
        if (device.empty()) {
            std::cerr << "3dp serve: missing --port <device> (e.g. COM3 or /dev/ttyUSB0)" << std::endl;
            return 2;
        }
        return serve(device, httpPort, dataDir);
    }
    return client(httpPort, rest[0], std::vector<std::string>(rest.begin() + 1, rest.end()));
}
