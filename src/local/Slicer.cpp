#include "Slicer.hpp"

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#define popen _popen
#define pclose _pclose
#else
#include <unistd.h>
#endif

namespace local {
    namespace {
        std::string quote(const std::string &text) {
            std::string out = "\"";
            for (const char c: text) {
                if (c == '"') out += "\\\"";
                else if (c == '\\') out += "\\\\";
                else if (c == '\n') out += "\\n";
                else if (static_cast<unsigned char>(c) >= 0x20) out += c;
            }
            return out + "\"";
        }

        std::string number(double value, int decimals) {
            char buffer[40];
            std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
            return buffer;
        }

        std::string counts(const std::map<std::string, size_t> &items) {
            std::string out = "{";
            bool first = true;
            for (const auto &[command, count]: items) {
                out += (first ? "" : ",") + quote(command) + ":" + std::to_string(count);
                first = false;
            }
            return out + "}";
        }

        std::string list(const std::vector<std::string> &items) {
            std::string out = "[";
            for (size_t i = 0; i < items.size(); ++i) out += (i ? "," : "") + quote(items[i]);
            return out + "]";
        }

        /// Un percorso va tra virgolette nel comando: non deve poterne uscire.
        bool safePath(const std::string &path) {
            for (const char c: path) {
                if (c == '"' || c == '\n' || c == '\r' || c == '`' || c == '$' || c == '%') return false;
            }
            return !path.empty();
        }

        bool parseNumber(const std::string &text, double low, double high, double &value) {
            try {
                size_t used = 0;
                value = std::stod(text, &used);
                return used == text.size() && value >= low && value <= high;
            } catch (...) {
                return false;
            }
        }

        std::string env(const char *name, const std::string &fallback) {
            const char *value = std::getenv(name);
            return value && *value ? value : fallback;
        }
    }

    std::string reportJson(const translator::gcode::FileReport &r) {
        std::ostringstream out;
        out << "{\"printable\":" << (r.printable() ? "true" : "false")
            << ",\"lines\":" << r.lines << ",\"commands\":" << r.commands << ",\"moves\":" << r.moves
            << ",\"arcs\":" << r.arcs << ",\"layers\":" << r.layers
            << ",\"homes\":" << (r.homes ? "true" : "false")
            << ",\"maxHotend\":" << number(r.maxHotend, 0)
            << ",\"filamentMm\":" << number(r.filamentMm, 1)
            << ",\"bounds\":{\"minX\":" << number(r.minX, 2) << ",\"maxX\":" << number(r.maxX, 2)
            << ",\"minY\":" << number(r.minY, 2) << ",\"maxY\":" << number(r.maxY, 2)
            << ",\"minZ\":" << number(r.minZ, 2) << ",\"maxZ\":" << number(r.maxZ, 2) << "}"
            << ",\"ignored\":" << counts(r.ignored) << ",\"unknown\":" << counts(r.unknown)
            << ",\"fatal\":" << counts(r.fatal)
            << ",\"problems\":" << list(r.problems) << ",\"warnings\":" << list(r.warnings) << "}";
        return out.str();
    }

    namespace {
        namespace fs = std::filesystem;

        /// Cartelle da cui risolvere i percorsi relativi: quella di lavoro e quella sopra l'eseguibile
        /// (build-local/3dp -> la radice del progetto).
        std::vector<fs::path> roots() {
            std::vector<fs::path> list{fs::current_path()};
#ifdef _WIN32
            char buffer[MAX_PATH];
            const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
            const fs::path self = length > 0 && length < MAX_PATH ? fs::path(std::string(buffer, length)) : fs::path();
#else
            std::error_code ec;
            const fs::path self = fs::read_symlink("/proc/self/exe", ec);
#endif
            if (!self.empty()) list.push_back(self.parent_path().parent_path());
            return list;
        }

        bool isFile(const fs::path &path) {
            std::error_code ec;
            return fs::is_regular_file(path, ec);
        }
    }

    SlicerSetup locateSlicer() {
        SlicerSetup setup;
#ifdef _WIN32
        const std::string name = "prusa-slicer-console.exe";
#else
        const std::string name = "prusa-slicer";
#endif
        const auto base = roots();

        // --- profilo ---
        setup.profile = env("THREEDP_SLICER_PROFILE", "config/slicer/3dp-prusaslicer.ini");
        setup.profileFound = isFile(setup.profile);
        for (size_t i = 0; i < base.size() && !setup.profileFound && fs::path(setup.profile).is_relative(); ++i) {
            const fs::path candidate = base[i] / setup.profile;
            if (isFile(candidate)) {
                setup.profile = candidate.generic_string();
                setup.profileFound = true;
            }
        }

        // --- eseguibile ---
        const std::string chosen = env("THREEDP_SLICER", "");
        if (!chosen.empty()) {
            setup.binary = chosen;
            // un nome senza cartella si cerca nel PATH al momento di lanciarlo
            setup.binaryFound = isFile(chosen) || chosen.find_first_of("/\\") == std::string::npos;
            if (setup.binaryFound) return setup;
        }
        std::error_code ec;
        for (const fs::path &root: base) {
            for (const char *folder: {"tools/slicer", "../_tools"}) {
                if (isFile(root / folder / name)) {
                    setup.binary = (root / folder / name).lexically_normal().generic_string();
                    setup.binaryFound = true;
                    return setup;
                }
                for (fs::directory_iterator it(root / folder, ec), end; !ec && it != end; it.increment(ec)) {
                    if (it->path().filename().string().rfind("PrusaSlicer", 0) != 0) continue;
                    if (!isFile(it->path() / name)) continue;
                    setup.binary = (it->path() / name).lexically_normal().generic_string();
                    setup.binaryFound = true;
                    return setup;
                }
            }
        }
#ifdef _WIN32
        for (const char *variable: {"ProgramFiles", "ProgramW6432"}) {
            const fs::path candidate = fs::path(env(variable, "C:/Program Files")) / "Prusa3D" / "PrusaSlicer" / name;
            if (isFile(candidate)) {
                setup.binary = candidate.generic_string();
                setup.binaryFound = true;
                return setup;
            }
        }
        const char separator = ';';
#else
        const char separator = ':';
#endif
        std::istringstream path(env("PATH", ""));
        std::string folder;
        while (std::getline(path, folder, separator)) {
            if (!folder.empty() && isFile(fs::path(folder) / name)) {
                setup.binary = (fs::path(folder) / name).generic_string();
                setup.binaryFound = true;
                return setup;
            }
        }
        if (setup.binary.empty()) setup.binary = name;
        return setup;
    }

    SliceResult slice(const std::string &modelPath, const std::string &outputPath,
                      const std::map<std::string, std::string> &options) {
        SliceResult result;
        const SlicerSetup setup = locateSlicer();
        const std::string binary = setup.binary, profile = setup.profile;
        if (!setup.binaryFound) {
            result.message = "PrusaSlicer not found: install it, or unpack the portable version in tools/slicer/, "
                             "or set its path in the configuration (Slicer)";
            return result;
        }

        if (!std::filesystem::exists(modelPath)) {
            result.message = "model not found: " + modelPath;
            return result;
        }
        if (!setup.profileFound) {
            result.message = "slicer profile not found: " + profile + " (set it in the configuration, Slicer)";
            return result;
        }
        if (!safePath(modelPath) || !safePath(outputPath) || !safePath(profile) || !safePath(binary)) {
            result.message = "path contains characters that are not allowed";
            return result;
        }

        // Solo parametri noti, solo numeri entro limiti sensati
        std::string extra;
        double v = 0;
        for (const auto &[key, text]: options) {
            if (key == "layer" && parseNumber(text, 0.05, 0.35, v)) {
                extra += " --layer-height " + number(v, 2);
            } else if (key == "infill" && parseNumber(text, 0, 100, v)) {
                extra += " --fill-density " + number(v, 0) + "%";
            } else if (key == "temp" && parseNumber(text, 170, 260, v)) {
                extra += " --temperature " + number(v, 0) + " --first-layer-temperature " +
                         number(v + 5 > 260 ? 260 : v + 5, 0);
            } else if (key == "perimeters" && parseNumber(text, 1, 8, v)) {
                extra += " --perimeters " + number(v, 0);
            } else if (key == "speed" && parseNumber(text, 10, 80, v)) {
                // velocita' dei perimetri; il resto segue in proporzione al profilo (45 mm/s di base)
                const double k = v / 45.0;
                extra += " --perimeter-speed " + number(v, 0) + " --infill-speed " + number(60 * k > 80 ? 80 : 60 * k, 0) +
                         " --external-perimeter-speed " + number(30 * k, 0) +
                         " --solid-infill-speed " + number(50 * k > 80 ? 80 : 50 * k, 0);
            } else if (key == "supports" && parseNumber(text, 0, 1, v)) {
                // opzione senza valore: "--support-material 0" verrebbe letto come un file di nome "0"
                extra += v > 0 ? " --support-material" : " --no-support-material";
            } else if (key == "brim" && parseNumber(text, 0, 20, v)) {
                extra += " --brim-width " + number(v, 1);
            } else {
                result.message = "option not accepted: " + key + "=" + text;
                return result;
            }
            result.parameters += (result.parameters.empty() ? "" : " ") + key + "=" + text;
        }

        const std::string command = "\"" + binary + "\" --export-gcode --load \"" + profile + "\"" + extra +
                                    " --center 110,110 -o \"" + outputPath + "\" \"" + modelPath + "\" 2>&1";
#ifdef _WIN32
        // cmd.exe toglie le virgolette esterne se la riga comincia con una virgoletta
        FILE *pipe = popen(("\"" + command + "\"").c_str(), "r");
#else
        FILE *pipe = popen(command.c_str(), "r");
#endif
        if (!pipe) {
            result.message = "cannot start the slicer (" + binary + ")";
            return result;
        }

        std::deque<std::string> tail;
        char buffer[512];
        while (std::fgets(buffer, sizeof(buffer), pipe)) {
            tail.emplace_back(buffer);
            if (tail.size() > 12) tail.pop_front();
        }
        const int status = pclose(pipe);
        for (const auto &line: tail) result.log += line;

        if (status != 0 || !std::filesystem::exists(outputPath)) {
            // il motivo vero e' nell'ultima riga non vuota scritta dallo slicer
            std::string reason;
            for (auto it = tail.rbegin(); it != tail.rend() && reason.empty(); ++it) {
                reason = *it;
                while (!reason.empty() && (reason.back() == '\n' || reason.back() == '\r' || reason.back() == ' ')) {
                    reason.pop_back();
                }
            }
            result.message = "slicing failed: " + (reason.empty() ? "the slicer wrote nothing (" + binary + ")" : reason);
            return result;
        }
        for (const auto &line: tail) {
            const size_t at = line.find("warning:");
            if (at == std::string::npos) continue;
            result.warning = line.substr(at + 8);
            while (!result.warning.empty() && (result.warning.back() == '\n' || result.warning.back() == '\r')) {
                result.warning.pop_back();
            }
            while (!result.warning.empty() && result.warning.front() == ' ') result.warning.erase(0, 1);
        }
        result.ok = true;
        result.gcodePath = outputPath;
        result.message = "sliced";
        return result;
    }
}
