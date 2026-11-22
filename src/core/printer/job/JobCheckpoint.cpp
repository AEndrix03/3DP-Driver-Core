#include "core/printer/job/JobCheckpoint.hpp"
#include "logger/Logger.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace core::print {
    namespace {
        fs::path pathFor(const std::string &jobId) {
            // il jobId arriva dall'esterno: nel nome del file solo caratteri sicuri
            std::string safe;
            for (char c: jobId) {
                safe += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
            }
            if (safe.empty()) safe = "job";
            return fs::path(JobCheckpoint::directory()) / (safe + ".ckpt");
        }

        std::optional<JobCheckpoint> read(const fs::path &file) {
            std::ifstream in(file);
            if (!in.is_open()) return std::nullopt;

            JobCheckpoint c;
            bool complete = false;
            std::string line;
            while (std::getline(in, line)) {
                const size_t eq = line.find('=');
                if (eq == std::string::npos) {
                    if (line == "end") complete = true;
                    continue;
                }
                const std::string key = line.substr(0, eq);
                const std::string value = line.substr(eq + 1);
                try {
                    if (key == "jobId") c.jobId = value;
                    else if (key == "gcodePath") c.gcodePath = value;
                    else if (key == "nextLine") c.nextLine = std::stoull(value);
                    else if (key == "positionKnown") c.positionKnown = value == "1";
                    else if (key == "x") c.x = std::stod(value);
                    else if (key == "y") c.y = std::stod(value);
                    else if (key == "z") c.z = std::stod(value);
                    else if (key == "e") c.e = std::stod(value);
                    else if (key == "feedrate") c.feedrate = std::stod(value);
                    else if (key == "absoluteXYZ") c.absoluteXYZ = value == "1";
                    else if (key == "absoluteE") c.absoluteE = value == "1";
                    else if (key == "hotendTarget") c.hotendTarget = std::stod(value);
                    else if (key == "fanSpeed") c.fanSpeed = std::stoi(value);
                    else if (key == "layer") c.layer = std::stoi(value);
                } catch (const std::exception &) {
                    return std::nullopt;
                }
            }
            // "end" manca se la scrittura e' stata interrotta: file da ignorare
            if (!complete || c.jobId.empty() || c.gcodePath.empty()) return std::nullopt;
            return c;
        }
    }

    std::string JobCheckpoint::directory() {
        return "checkpoints";
    }

    bool JobCheckpoint::save() const {
        try {
            fs::create_directories(directory());
            const fs::path target = pathFor(jobId);
            const fs::path temporary = target.string() + ".tmp";
            {
                std::ofstream out(temporary, std::ios::trunc);
                if (!out.is_open()) return false;
                out << std::setprecision(10);
                out << "jobId=" << jobId << "\n"
                    << "gcodePath=" << gcodePath << "\n"
                    << "nextLine=" << nextLine << "\n"
                    << "positionKnown=" << (positionKnown ? 1 : 0) << "\n"
                    << "x=" << x << "\n" << "y=" << y << "\n" << "z=" << z << "\n"
                    << "e=" << e << "\n"
                    << "feedrate=" << feedrate << "\n"
                    << "absoluteXYZ=" << (absoluteXYZ ? 1 : 0) << "\n"
                    << "absoluteE=" << (absoluteE ? 1 : 0) << "\n"
                    << "hotendTarget=" << hotendTarget << "\n"
                    << "fanSpeed=" << fanSpeed << "\n"
                    << "layer=" << layer << "\n"
                    << "end\n";
                if (!out.good()) return false;
            }
            fs::rename(temporary, target); // sostituzione atomica: mai un checkpoint scritto a meta'
            return true;
        } catch (const std::exception &e) {
            Logger::logError("[JobCheckpoint] Save failed: " + std::string(e.what()));
            return false;
        }
    }

    void JobCheckpoint::remove(const std::string &jobId) {
        std::error_code ec;
        fs::remove(pathFor(jobId), ec);
    }

    std::optional<JobCheckpoint> JobCheckpoint::load(const std::string &jobId) {
        return read(pathFor(jobId));
    }

    std::optional<JobCheckpoint> JobCheckpoint::latest() {
        std::error_code ec;
        std::optional<fs::path> newest;
        fs::file_time_type newestTime{};

        for (const auto &entry: fs::directory_iterator(directory(), ec)) {
            if (entry.path().extension() != ".ckpt") continue;
            const auto time = entry.last_write_time(ec);
            if (!newest || time > newestTime) {
                newest = entry.path();
                newestTime = time;
            }
        }
        return newest ? read(*newest) : std::nullopt;
    }

} // namespace core::print
