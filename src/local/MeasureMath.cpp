#include "MeasureMath.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace local::measure {

    Stats stats(const std::vector<double> &values) {
        Stats s;
        s.count = static_cast<int>(values.size());
        if (values.empty()) return s;
        s.min = s.max = values[0];
        double sum = 0.0;
        for (const double v: values) {
            sum += v;
            s.min = std::min(s.min, v);
            s.max = std::max(s.max, v);
        }
        s.mean = sum / s.count;
        double squares = 0.0;
        for (const double v: values) squares += (v - s.mean) * (v - s.mean);
        s.deviation = s.count > 1 ? std::sqrt(squares / (s.count - 1)) : 0.0;
        return s;
    }

    namespace {
        /// Le parole della riga che inizia con `tag`, senza il checksum finale ("*123").
        std::vector<std::string> words(const std::string &line, const char *tag) {
            std::vector<std::string> out;
            const size_t at = line.find(tag);
            if (at == std::string::npos) return out;
            std::istringstream in(line.substr(at));
            std::string word;
            while (in >> word) {
                if (word[0] == '*') break;
                out.push_back(word);
            }
            return out;
        }
    }

    Homing parseHoming(const std::vector<std::string> &body) {
        Homing h;
        for (const std::string &line: body) {
            const auto w = words(line, "HOM ");
            if (w.size() < 3 || w[1].size() != 1 || w[1][0] < 'X' || w[1][0] > 'Z') continue;
            const int axis = w[1][0] - 'X';
            if (w[2] == "SKIP") h.skipped[axis] = true;
            else if (w[2] == "FAIL") h.failed[axis] = true;
            else if (w[2] == "OK") {
                h.done[axis] = true;
                if (w.size() > 3) h.offset[axis] = std::strtol(w[3].c_str(), nullptr, 10);
            }
        }
        return h;
    }

    Health parseHealth(const std::vector<std::string> &body) {
        Health h;
        bool dia = false, prf = false;
        for (const std::string &line: body) {
            // DIA <uptime> <reset> <ultimo N> <err cs> <err overflow> <ACK persi> <RAM libera>
            if (const auto w = words(line, "DIA "); w.size() >= 7) {
                h.checksumErrors = std::atol(w[4].c_str());
                h.overflows = std::atol(w[5].c_str());
                h.lostAcks = std::atol(w[6].c_str());
                dia = true;
            }
            // PRF <RAM minima> <durata max interrupt, us> <scadenze mancate> <errori UART>
            if (const auto w = words(line, "PRF "); w.size() >= 5) {
                h.freeRamMin = std::atol(w[1].c_str());
                h.stepInterruptMaxUs = std::atol(w[2].c_str());
                h.missedDeadlines = std::atol(w[3].c_str());
                h.uartErrors = std::atol(w[4].c_str());
                prf = true;
            }
        }
        h.valid = dia && prf;
        return h;
    }

    double peakSpeed(double requested, double acceleration, double distance) {
        if (acceleration <= 0.0 || distance <= 0.0) return requested;
        return std::min(requested, std::sqrt(acceleration * distance)); // meta' tratto per salire, meta' per frenare
    }

    PidProposal tuneFromRise(const std::vector<TempSample> &samples, double fullPowerBelow) {
        PidProposal p;
        std::vector<TempSample> rise;
        for (const TempSample &s: samples) {
            if (s.celsius >= fullPowerBelow) break;
            rise.push_back(s);
        }
        if (rise.size() < 8) {
            p.why = "troppo pochi campioni a piena potenza";
            return p;
        }
        const double start = rise.front().celsius;
        if (rise.back().celsius - start < 30.0) {
            p.why = "salita a piena potenza inferiore a 30 gradi: partire da piu' freddo";
            return p;
        }

        // Pendenza massima: retta ai minimi quadrati su una finestra di 4 s che scorre lungo la salita
        constexpr double WINDOW = 4.0;
        double bestSlope = 0.0, bestT = 0.0, bestC = 0.0;
        for (size_t a = 0; a < rise.size(); ++a) {
            size_t b = a;
            while (b + 1 < rise.size() && rise[b].t - rise[a].t < WINDOW) ++b;
            if (rise[b].t - rise[a].t < WINDOW * 0.75 || b - a < 3) continue;
            double st = 0, sc = 0;
            const double n = static_cast<double>(b - a + 1);
            for (size_t i = a; i <= b; ++i) {
                st += rise[i].t;
                sc += rise[i].celsius;
            }
            const double mt = st / n, mc = sc / n;
            double num = 0, den = 0;
            for (size_t i = a; i <= b; ++i) {
                num += (rise[i].t - mt) * (rise[i].celsius - mc);
                den += (rise[i].t - mt) * (rise[i].t - mt);
            }
            if (den <= 0.0) continue;
            const double slope = num / den;
            if (slope > bestSlope) {
                bestSlope = slope;
                bestT = mt;
                bestC = mc;
            }
        }
        if (bestSlope < 0.05) {
            p.why = "la temperatura non sale abbastanza in fretta per misurare la pendenza";
            return p;
        }
        // Ritardo: dove la tangente nel punto di massima pendenza incontra la temperatura di partenza
        p.rate = bestSlope;
        p.deadTime = std::max(0.5, bestT - (bestC - start) / bestSlope - rise.front().t);

        // Regola della curva di reazione (Ziegler-Nichols), con guadagno e integrale ridotti: su un
        // hotend conta non superare il target piu' che arrivarci qualche secondo prima.
        // L'uscita del PID del firmware va da 0 a 255, quindi la pendenza per unita' di uscita e' rate/255.
        const double unit = p.rate / 255.0;
        p.kp = 0.9 / (unit * p.deadTime);
        p.ki = p.kp / (6.0 * p.deadTime);
        p.kd = p.kp * 0.5 * p.deadTime;
        p.valid = true;
        return p;
    }
}
