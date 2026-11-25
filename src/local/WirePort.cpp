#include "WirePort.hpp"

#include "core/MoveFrame.hpp"

#include <cstdio>
#include <cstdlib>

namespace local {
    namespace {
        constexpr size_t KEPT = 600; ///< scambi tenuti in memoria

        std::string toHex(const std::string &bytes) {
            static const char digits[] = "0123456789ABCDEF";
            std::string out;
            out.reserve(bytes.size() * 3);
            for (const char c: bytes) {
                const auto b = static_cast<uint8_t>(c);
                if (!out.empty()) out.push_back(' ');
                out.push_back(digits[b >> 4]);
                out.push_back(digits[b & 15]);
            }
            return out;
        }

        std::string quoted(const std::string &text) {
            std::string out = "\"";
            for (const char c: text) {
                if (c == '"' || c == '\\') {
                    out.push_back('\\');
                    out.push_back(c);
                } else if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) >= 0x7F) {
                    out.push_back('.'); // byte non stampabile in una riga di testo (rumore sulla linea)
                } else {
                    out.push_back(c);
                }
            }
            return out + "\"";
        }

        /// Valore che segue `key` in una riga come "OK0 N12 D9" (-1 se manca)
        long long fieldAfter(const std::string &line, char key) {
            for (size_t i = 0; i + 1 < line.size(); ++i) {
                if (line[i] == key && (i == 0 || line[i - 1] == ' ') &&
                    line[i + 1] >= '0' && line[i + 1] <= '9') {
                    return std::atoll(line.c_str() + i + 1);
                }
            }
            return -1;
        }

        /// Nome di un comando di testo ("N12 M115 *72" -> PLANNING_DATA): gli stessi dei contratti del firmware.
        std::string commandName(const std::string &line) {
            static const struct { char category; int code; const char *name; } table[] = {
                    {'M', 0, "EMERGENCY_STOP"}, {'M', 12, "SET_POSITION"}, {'M', 99, "DIAGNOSE_AXIS"},
                    {'M', 114, "GET_POSITION"}, {'M', 115, "PLANNING_DATA"},
                    {'S', 0, "HOMING"}, {'S', 1, "START_PRINT"}, {'S', 2, "PAUSE"}, {'S', 3, "RESUME"},
                    {'S', 4, "EMERGENCY_RESET"}, {'S', 5, "BRUTAL_RESET"}, {'S', 6, "CLEAR_ERROR"},
                    {'S', 7, "ERROR_STATUS"}, {'S', 8, "POWER_STATUS"}, {'S', 9, "FORCE_POWER"},
                    {'S', 10, "PRINT_STATUS"}, {'S', 11, "DIAGNOSTICS"}, {'S', 12, "END_PRINT"},
                    {'T', 0, "HEATER_OFF"}, {'T', 10, "SET_TEMPERATURE"}, {'T', 20, "GET_TEMPERATURE"},
                    {'T', 25, "PROTECTION_STATUS"}, {'T', 30, "SET_PROTECTION"},
                    {'F', 10, "SET_FAN"}, {'E', 10, "READ_ENDSTOPS"},
                    {'A', 0, "EXTRUDER_STOP"}, {'A', 10, "EXTRUDE"}, {'A', 11, "RETRACT"}, {'A', 12, "PURGE"},
                    {'A', 13, "EXTRUDER_STEP"}, {'A', 14, "RESET_E_POSITION"}, {'A', 15, "SET_E_STEPS"},
                    {'A', 16, "GET_E_POSITION"},
                    {'H', 10, "PRINT_HISTORY"}, {'H', 20, "RESEND"}, {'H', 30, "CLEAR_HISTORY"},
                    {'H', 40, "LAST_NUMBER"},
            };
            // dopo "N<numero> " viene la lettera della categoria e il codice
            size_t at = 0;
            if (!line.empty() && line[0] == 'N') {
                at = line.find(' ');
                if (at == std::string::npos) return "";
                ++at;
            }
            if (at + 1 >= line.size() || line[at + 1] < '0' || line[at + 1] > '9') return "";
            const char category = line[at];
            const int code = std::atoi(line.c_str() + at + 1);
            for (const auto &entry: table) {
                if (entry.category == category && entry.code == code) return entry.name;
            }
            return std::string(1, category) + std::to_string(code);
        }

        /// Che cos'e' una riga arrivata dalla scheda: OK, BUSY, ERROR, oppure il tipo di dato (POS, TMP, LIM...)
        std::string replyName(const std::string &line) {
            if (line.rfind("OK", 0) == 0) return "OK";
            if (line.find("ERR") != std::string::npos || line.rfind("RS", 0) == 0) return "ERROR";
            if (line.rfind("BUSY", 0) == 0) return "BUSY";
            size_t from = line.rfind("CRT ", 0) == 0 ? 4 : 0;
            size_t to = from;
            while (to < line.size() && line[to] >= 'A' && line[to] <= 'Z') ++to;
            return to > from && to - from <= 12 ? line.substr(from, to - from) : "";
        }

        struct Frame {
            uint8_t sequence = 0;
            long long steps[4] = {0, 0, 0, 0};
            bool hasPlan = false;
            double speed = 0.0, acceleration = 0.0;
        };

        /// Stessa lettura di MoveFrame::describe, ma con i valori.
        bool parseFrame(const std::string &frame, Frame &out) {
            if (!core::MoveFrame::isFrame(frame) || frame.size() < 4) return false;
            const auto header = static_cast<uint8_t>(frame[0]);
            const int sextets = (header & 0x10) ? 3 : 2;
            out.sequence = static_cast<uint8_t>(frame[1]) & 0x3F;
            size_t at = 2;
            const auto field = [&frame, &at](int count) {
                uint32_t value = 0;
                for (int i = 0; i < count && at < frame.size(); ++i) {
                    value = (value << 6) | (static_cast<uint8_t>(frame[at++]) & 0x3F);
                }
                return value;
            };
            for (int axis = 0; axis < 4; ++axis) {
                if (!(header & (0x08 >> axis))) continue;
                const uint32_t value = field(sextets);
                const uint32_t sign = 1u << (sextets * 6 - 1);
                out.steps[axis] = (value & sign) ? static_cast<long long>(value) - static_cast<long long>(sign) * 2
                                                 : static_cast<long long>(value);
            }
            out.hasPlan = (header & 0x20) != 0;
            if (out.hasPlan) {
                out.speed = field(3) / 64.0;
                out.acceleration = field(3);
            }
            return true;
        }
    }

    WirePort::WirePort(std::shared_ptr<core::SerialPort> inner) : inner_(std::move(inner)) {}

    void WirePort::record(char direction, const char *kind, const std::string &name, const std::string &hex,
                          const std::string &text) {
        Entry entry;
        entry.name = name;
        entry.id = nextId_++;
        entry.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
        entry.direction = direction;
        entry.kind = kind;
        entry.hex = hex;
        entry.text = text;
        entries_.push_back(std::move(entry));
        if (entries_.size() > KEPT) entries_.pop_front();
    }

    void WirePort::completedUpTo(uint32_t number) {
        while (!pending_.empty() && pending_.front().number <= number) pending_.pop_front();
    }

    void WirePort::send(const std::string &data) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++counters_.textTx;
            counters_.bytesTx += data.size() + 1;
            const long long number = fieldAfter(data, 'N');
            if (number >= 0) lastNumber_ = static_cast<uint32_t>(number);
            record('T', "text", commandName(data), "", data);
        }
        inner_->send(data);
    }

    void WirePort::sendRaw(const std::string &bytes) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            counters_.bytesTx += bytes.size();
            Frame frame;
            if (parseFrame(bytes, frame)) {
                bool again = false;
                for (const Pending &p: pending_) {
                    if ((p.number & 0x3F) == frame.sequence) again = true;
                }
                if (frame.hasPlan) {
                    modalSpeed_ = frame.speed;
                    modalAcceleration_ = frame.acceleration;
                }
                if (again) {
                    ++counters_.resent;
                } else {
                    ++counters_.framesTx;
                    // Il frame porta solo il numero modulo 64: quello intero e' il primo dopo l'ultimo inviato
                    uint32_t number = (lastNumber_ & ~0x3Fu) | frame.sequence;
                    if (number <= lastNumber_) number += 64;
                    lastNumber_ = number;
                    Pending pending;
                    pending.number = number;
                    for (int a = 0; a < 4; ++a) pending.steps[a] = frame.steps[a];
                    pending.speed = modalSpeed_;
                    pending.acceleration = modalAcceleration_;
                    pending_.push_back(pending);
                    if (pending_.size() > 64) pending_.pop_front(); // mai tanti in volo: difesa da risposte perse
                }
                record('T', again ? "resend" : "move", again ? "MOVE (di nuovo)" : "MOVE", toHex(bytes),
                       core::MoveFrame::describe(bytes));
            } else {
                record('T', "raw", "", toHex(bytes), "");
            }
        }
        inner_->sendRaw(bytes);
    }

    std::string WirePort::receiveLine() {
        std::string line = inner_->receiveLine();
        if (line.empty()) return line;

        std::lock_guard<std::mutex> lock(mutex_);
        counters_.bytesRx += line.size() + 1;
        core::MoveFrame::Ack ack;
        if (core::MoveFrame::decodeAck(line, ack)) {
            ++counters_.acksRx;
            // la conferma riguarda il frame piu' recente con quel numero modulo 64
            for (auto it = pending_.rbegin(); it != pending_.rend(); ++it) {
                if ((it->number & 0x3F) != ack.sequence) continue;
                if (ack.lag != core::MoveFrame::ACK_LAG_UNKNOWN && ack.lag <= it->number) {
                    completedUpTo(it->number - ack.lag);
                }
                break;
            }
            char text[64];
            if (ack.lag == core::MoveFrame::ACK_LAG_UNKNOWN) {
                std::snprintf(text, sizeof(text), "ACK #%u, coda 63 o piu'", static_cast<unsigned>(ack.sequence));
            } else {
                std::snprintf(text, sizeof(text), "ACK #%u, %u da completare", static_cast<unsigned>(ack.sequence),
                              static_cast<unsigned>(ack.lag));
            }
            record('R', "ack", "ACK", toHex(line + "\n"), text);
            return line;
        }

        ++counters_.textRx;
        if (line.rfind("OK", 0) == 0) {
            const long long done = fieldAfter(line, 'D');
            if (done >= 0) completedUpTo(static_cast<uint32_t>(done));
        } else if (line.find("ERR") != std::string::npos || line.rfind("RS", 0) == 0 ||
                   line.find('~') != std::string::npos) { // '~' = risposta troppo lunga, tagliata dal firmware
            ++counters_.errors;
            if (errorLines_.size() < 200) errorLines_.push_back(line);
        }
        record('R', "text", replyName(line), "", line);
        return line;
    }

    std::vector<std::string> WirePort::takeErrors() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> lines;
        lines.swap(errorLines_);
        return lines;
    }

    WirePort::Live WirePort::live() {
        std::lock_guard<std::mutex> lock(mutex_);
        Live result = counters_;
        for (const Pending &p: pending_) {
            for (int a = 0; a < 4; ++a) result.outstanding[a] += p.steps[a];
        }
        result.queued = static_cast<int>(pending_.size());
        if (!pending_.empty()) {
            result.speed = pending_.front().speed;
            result.acceleration = pending_.front().acceleration;
        }
        return result;
    }

    std::string WirePort::json(uint64_t after, size_t limit) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t first = 0;
        while (first < entries_.size() && entries_[first].id <= after) ++first;
        size_t skipped = 0;
        if (entries_.size() - first > limit) {
            skipped = entries_.size() - first - limit;
            first += skipped;
        }
        // quelli gia' usciti dalla memoria contano come saltati
        if (!entries_.empty() && after + 1 < entries_.front().id && after != 0) {
            skipped += static_cast<size_t>(entries_.front().id - after - 1);
        }

        std::string out = "\"last\":" + std::to_string(nextId_ - 1) + ",\"skipped\":" + std::to_string(skipped) +
                          ",\"wire\":[";
        char head[96];
        for (size_t i = first; i < entries_.size(); ++i) {
            const Entry &e = entries_[i];
            std::snprintf(head, sizeof(head), "%s{\"id\":%llu,\"t\":%.3f,\"d\":\"%c\",\"k\":\"%s\",\"n\":\"",
                          i == first ? "" : ",", static_cast<unsigned long long>(e.id), e.seconds, e.direction, e.kind);
            out += head;
            out += e.name; // solo lettere maiuscole, cifre, '_', spazio e parentesi: niente da proteggere
            out += "\",\"hex\":\"";
            out += e.hex;
            out += "\",\"text\":" + quoted(e.text) + "}";
        }
        return out + "]";
    }
}
