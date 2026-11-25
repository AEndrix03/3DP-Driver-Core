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

    void WirePort::record(char direction, const char *kind, const std::string &hex, const std::string &text) {
        Entry entry;
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
            record('T', "text", "", data);
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
                record('T', again ? "resend" : "move", toHex(bytes), core::MoveFrame::describe(bytes));
            } else {
                record('T', "raw", toHex(bytes), "");
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
            record('R', "ack", toHex(line + "\n"), text);
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
        record('R', "text", "", line);
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
            std::snprintf(head, sizeof(head), "%s{\"id\":%llu,\"t\":%.3f,\"d\":\"%c\",\"k\":\"%s\",\"hex\":\"",
                          i == first ? "" : ",", static_cast<unsigned long long>(e.id), e.seconds, e.direction, e.kind);
            out += head;
            out += e.hex;
            out += "\",\"text\":" + quoted(e.text) + "}";
        }
        return out + "]";
    }
}
