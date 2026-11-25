#include "Settings.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace local {
    namespace {
        std::string quote(const std::string &text) {
            std::string out = "\"";
            for (const char c: text) {
                if (c == '"' || c == '\\') out.push_back('\\');
                if (static_cast<unsigned char>(c) >= 0x20) out.push_back(c);
            }
            return out + "\"";
        }

        std::string plain(double value) {
            char buffer[40];
            std::snprintf(buffer, sizeof(buffer), "%.6g", value);
            return buffer;
        }
    }

    const std::vector<Setting> &Settings::catalog() {
        // 678 movimenti al secondo: il massimo misurato sulla scheda (PROTOCOL.md del firmware)
        static const std::vector<Setting> items = {
                {"speed_percent", "Velocita'", "Velocita' del lavoro", "%", 5, 100, 1, 100, false, true,
                        "Moltiplica le velocita' chieste dal G-code. E' la leva giusta se la meccanica non regge: tutto rallenta in proporzione."},
                {"blocks_per_second", "Velocita'", "Movimenti inviati al secondo", "/s", 10, 678, 1, 678, false, true,
                        "Tetto ai movimenti mandati alla scheda. 678 e' il massimo misurato. Abbassandolo, i tratti corti vengono percorsi piu' piano in modo regolare."},
                {"accel_print", "Velocita'", "Accelerazione in stampa", "mm/s2", 0, 5000, 10, 0, false, true,
                        "0 = quella massima della macchina. Un M204 nel file la sostituisce."},
                {"accel_travel", "Velocita'", "Accelerazione negli spostamenti", "mm/s2", 0, 5000, 10, 0, false, true,
                        "0 = quella massima della macchina. Un M204 nel file la sostituisce."},
                {"linear_advance", "Velocita'", "Linear advance K", "s", 0, 2, 0.005, 0, false, true,
                        "Compensa la pressione nell'ugello. 0 = spento. Da tarare: 0,03-0,1 con estrusore diretto, 0,3-0,8 con bowden."},

                {"max_feed_xy", "Limiti della macchina", "Velocita' massima X e Y", "mm/s", 0, 500, 1, 0, false, false,
                        "0 = il valore del firmware. Si puo' solo abbassare: oltre il firmware non si va."},
                {"max_feed_z", "Limiti della macchina", "Velocita' massima Z", "mm/s", 0, 100, 0.1, 0, false, false,
                        "0 = il valore del firmware."},
                {"max_feed_e", "Limiti della macchina", "Velocita' massima estrusore", "mm/s", 0, 200, 1, 0, false, false,
                        "0 = il valore del firmware."},
                {"max_accel_xy", "Limiti della macchina", "Accelerazione massima X e Y", "mm/s2", 0, 10000, 10, 0, false, false,
                        "0 = il valore del firmware."},
                {"max_accel_z", "Limiti della macchina", "Accelerazione massima Z", "mm/s2", 0, 2000, 10, 0, false, false,
                        "0 = il valore del firmware."},
                {"max_accel_e", "Limiti della macchina", "Accelerazione massima estrusore", "mm/s2", 0, 10000, 10, 0, false, false,
                        "0 = il valore del firmware."},
                {"junction_deviation", "Limiti della macchina", "Deviazione di giunzione", "mm", 0, 0.2, 0.001, 0, false, false,
                        "Quanto si rallenta negli angoli: piu' bassa = angoli piu' lenti. 0 = il valore del firmware."},

                {"volume_x", "Volume di lavoro", "Corsa X", "mm", 10, 1000, 1, 220, false, false,
                        "Usata per controllare i file prima di stamparli e per disegnare il piano."},
                {"volume_y", "Volume di lavoro", "Corsa Y", "mm", 10, 1000, 1, 220, false, false, ""},
                {"volume_z", "Volume di lavoro", "Corsa Z", "mm", 10, 1000, 1, 200, false, false, ""},

                {"preheat", "Ugello", "Temperatura di preriscaldamento", "C", 0, 280, 1, 200, false, true,
                        "Valore proposto dal tasto Scalda."},
                {"pid_p", "Ugello", "PID: proporzionale", "", 0, 500, 0.01, 0, false, false,
                        "Tutti e tre a 0 = restano i guadagni del firmware. Altrimenti vengono inviati alla scheda a ogni collegamento."},
                {"pid_i", "Ugello", "PID: integrale", "", 0, 500, 0.001, 0, false, false, ""},
                {"pid_d", "Ugello", "PID: derivativo", "", 0, 5000, 0.01, 0, false, false, ""},

                {"slicer_path", "Slicer", "Eseguibile di PrusaSlicer", "", 0, 0, 0, 0, true, true,
                        "Vuoto = prusa-slicer nel PATH (su Windows prusa-slicer-console.exe)."},
                {"slicer_profile", "Slicer", "Profilo della macchina", "", 0, 0, 0, 0, true, true,
                        "Vuoto = config/slicer/3dp-prusaslicer.ini."},
                {"slice_layer", "Slicer", "Altezza dello strato proposta", "mm", 0.05, 0.4, 0.05, 0.2, false, true, ""},
                {"slice_infill", "Slicer", "Riempimento proposto", "%", 0, 100, 1, 20, false, true, ""},
                {"slice_temp", "Slicer", "Temperatura proposta", "C", 150, 280, 1, 205, false, true, ""},
        };
        return items;
    }

    const Setting *Settings::find(const std::string &key) {
        for (const Setting &s: catalog()) {
            if (key == s.key) return &s;
        }
        return nullptr;
    }

    Settings::Settings(const std::string &path) : path_(path) {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const size_t eq = line.find('=');
            if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
            const std::string key = line.substr(0, eq);
            if (find(key)) values_[key] = line.substr(eq + 1);
        }
    }

    double Settings::number(const std::string &key) {
        std::lock_guard<std::mutex> lock(mutex_);
        const Setting *s = find(key);
        if (!s) return 0.0;
        const auto it = values_.find(key);
        if (it == values_.end() || it->second.empty()) return s->fallback;
        return std::atof(it->second.c_str());
    }

    std::string Settings::text(const std::string &key) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = values_.find(key);
        return it == values_.end() ? "" : it->second;
    }

    std::string Settings::set(const std::string &key, const std::string &value) {
        const Setting *s = find(key);
        if (!s) return "unknown setting: " + key;
        std::lock_guard<std::mutex> lock(mutex_);
        if (s->text) {
            if (value.find('\n') != std::string::npos || value.size() > 500) return std::string(s->label) + ": not valid";
            values_[key] = value;
            return "";
        }
        char *end = nullptr;
        const double parsed = std::strtod(value.c_str(), &end);
        if (value.empty() || end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) {
            return std::string(s->label) + ": not a number";
        }
        if (parsed < s->min || parsed > s->max) {
            return std::string(s->label) + ": allowed " + plain(s->min) + " .. " + plain(s->max);
        }
        values_[key] = plain(parsed);
        return "";
    }

    bool Settings::save() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ofstream out(path_, std::ios::trunc);
        out << "# Configurazione di 3dp: si modifica dalla pagina (Configurazione) o qui a servizio fermo.\n";
        for (const Setting &s: catalog()) {
            const auto it = values_.find(s.key);
            if (it != values_.end()) out << s.key << "=" << it->second << "\n";
        }
        return out.good();
    }

    std::string Settings::json() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string out = "\"groups\":[";
        std::string group;
        bool firstGroup = true, firstItem = true;
        for (const Setting &s: catalog()) {
            if (group != s.group) {
                if (!firstGroup) out += "]},";
                firstGroup = false;
                firstItem = true;
                group = s.group;
                out += "{\"name\":" + quote(group) + ",\"items\":[";
            }
            const auto it = values_.find(s.key);
            const bool has = it != values_.end() && !it->second.empty();
            out += firstItem ? "" : ",";
            firstItem = false;
            out += "{\"key\":" + quote(s.key) + ",\"label\":" + quote(s.label) + ",\"unit\":" + quote(s.unit) +
                   ",\"help\":" + quote(s.help) + ",\"text\":" + (s.text ? "true" : "false") +
                   ",\"whilePrinting\":" + (s.whilePrinting ? "true" : "false");
            if (s.text) {
                out += ",\"value\":" + quote(has ? it->second : "");
            } else {
                out += ",\"min\":" + plain(s.min) + ",\"max\":" + plain(s.max) + ",\"step\":" + plain(s.step) +
                       ",\"fallback\":" + plain(s.fallback) + ",\"value\":" + (has ? it->second : plain(s.fallback));
            }
            out += "}";
        }
        return out + (firstGroup ? "]" : "]}]");
    }
}
