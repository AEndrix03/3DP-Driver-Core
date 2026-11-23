#include "Slicer.hpp"

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <sstream>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
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

    SliceResult slice(const std::string &modelPath, const std::string &outputPath,
                      const std::map<std::string, std::string> &options) {
        SliceResult result;
#ifdef _WIN32
        const std::string binary = env("THREEDP_SLICER", "prusa-slicer-console.exe");
#else
        const std::string binary = env("THREEDP_SLICER", "prusa-slicer");
#endif
        const std::string profile = env("THREEDP_SLICER_PROFILE", "config/slicer/3dp-prusaslicer.ini");

        if (!std::filesystem::exists(modelPath)) {
            result.message = "model not found: " + modelPath;
            return result;
        }
        if (!std::filesystem::exists(profile)) {
            result.message = "slicer profile not found: " + profile + " (set THREEDP_SLICER_PROFILE)";
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
                extra += std::string(" --support-material ") + (v > 0 ? "1" : "0");
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
            result.message = "slicing failed (is " + binary + " installed? set THREEDP_SLICER to its path)";
            return result;
        }
        result.ok = true;
        result.gcodePath = outputPath;
        result.message = "sliced";
        return result;
    }
}
