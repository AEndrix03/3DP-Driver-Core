#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

namespace core {

/**
 * @brief Codifica dei movimenti in formato binario (vedi MoveFrame.hpp e PROTOCOL.md
 * nel repo del firmware).
 *
 * Ogni byte ha il bit 7 a 1 e porta 6 bit utili: l'intestazione e' 11xxxxxx, gli
 * altri byte 10xxxxxx. Un frame non puo' quindi contenere caratteri di testo, fine
 * riga o il carattere di arresto '!'.
 */
    namespace MoveFrame {
        constexpr int32_t NARROW_LIMIT = 2047;    ///< campo a 2 sestetti (12 bit con segno)
        constexpr int32_t STEP_LIMIT = 32767;     ///< step massimi per asse in un movimento
        constexpr uint32_t ENTRY_LIMIT = 4095;    ///< velocita' di ingresso, 1/4 mm/s (12 bit)
        constexpr size_t MAX_LENGTH = 27;

        /// Un movimento gia' pianificato, nelle unita' del firmware (vedi Planner.hpp del firmware).
        struct PlannedMove {
            int32_t steps[4] = {0, 0, 0, 0}; ///< X, Y, Z, E con segno, entro STEP_LIMIT
            uint16_t nominalSpeed = 0;       ///< 1/64 mm/s
            uint16_t acceleration = 0;       ///< mm/s^2
            uint16_t stepScale = 0;          ///< step dell'asse dominante per mm, in 1/16
            uint16_t entryQuarter = 0;       ///< velocita' massima di ingresso, 1/4 mm/s
        };

        inline uint16_t crcUpdate(uint16_t crc, uint8_t data) {
            data ^= static_cast<uint8_t>(crc & 0xFF);
            data ^= static_cast<uint8_t>(data << 4);
            return static_cast<uint16_t>(((static_cast<uint16_t>(data) << 8) | (crc >> 8)) ^
                                         static_cast<uint8_t>(data >> 4) ^ (static_cast<uint16_t>(data) << 3));
        }

        inline bool isFrame(const std::string &data) {
            return !data.empty() && static_cast<uint8_t>(data[0]) >= 0xC0;
        }

        /**
         * @param number numero del comando (ne viene trasmesso il modulo 64)
         * @param withPlan true = include velocita' nominale e accelerazione; false = il firmware
         *        riusa quelle del movimento precedente
         */
        inline std::string encode(uint32_t number, const PlannedMove &move, bool withPlan) {
            bool wide = false;
            uint8_t header = 0xC0;
            for (int axis = 0; axis < 4; ++axis) {
                if (move.steps[axis] == 0) continue;
                header |= static_cast<uint8_t>(0x08 >> axis);
                if (std::abs(move.steps[axis]) > NARROW_LIMIT) wide = true;
            }
            if (wide) header |= 0x10;
            if (withPlan) header |= 0x20;

            std::string frame;
            frame.push_back(static_cast<char>(header));
            frame.push_back(static_cast<char>(0x80 | (number & 0x3F)));

            const auto put = [&frame](uint32_t value, int sextets) {
                for (int i = sextets - 1; i >= 0; --i) {
                    frame.push_back(static_cast<char>(0x80 | ((value >> (6 * i)) & 0x3F)));
                }
            };
            for (int axis = 0; axis < 4; ++axis) {
                // complemento a due, troncato alla larghezza del campo
                if (move.steps[axis] != 0) put(static_cast<uint32_t>(move.steps[axis]), wide ? 3 : 2);
            }
            if (withPlan) {
                put(move.nominalSpeed, 3);
                put(move.acceleration, 3);
            }
            put(move.stepScale, 3);
            put(move.entryQuarter > ENTRY_LIMIT ? ENTRY_LIMIT : move.entryQuarter, 2);

            uint16_t crc = 0xFFFF;
            for (const char c: frame) crc = crcUpdate(crc, static_cast<uint8_t>(c));
            put(crc & 0x0FFF, 2);
            return frame;
        }

        /// Conferma corta di un movimento accettato (vedi MoveFrame.hpp del firmware).
        struct Ack {
            uint8_t sequence = 0; ///< numero del comando modulo 64
            uint8_t lag = 0;      ///< comandi ancora da completare; ACK_LAG_UNKNOWN = 63 o piu'
        };

        constexpr uint8_t ACK_LAG_UNKNOWN = 63;

        /// @return true se `line` (senza fine riga) e' una conferma integra
        inline bool decodeAck(const std::string &line, Ack &ack) {
            if (line.size() != 4) return false;
            for (const char c: line) {
                if ((static_cast<uint8_t>(c) & 0xC0) != 0x80) return false;
            }
            uint16_t crc = crcUpdate(0xFFFF, static_cast<uint8_t>(line[0]));
            crc = crcUpdate(crc, static_cast<uint8_t>(line[1]));
            const uint16_t received = static_cast<uint16_t>(((static_cast<uint8_t>(line[2]) & 0x3F) << 6) |
                                                            (static_cast<uint8_t>(line[3]) & 0x3F));
            if ((crc & 0x0FFF) != received) return false;
            ack.sequence = static_cast<uint8_t>(line[0]) & 0x3F;
            ack.lag = static_cast<uint8_t>(line[1]) & 0x3F;
            return true;
        }

        /// Testo leggibile di un frame, per la traccia della seriale.
        inline std::string describe(const std::string &frame) {
            if (!isFrame(frame) || frame.size() < 4) return "<frame non valido>";
            const uint8_t header = static_cast<uint8_t>(frame[0]);
            const int sextets = (header & 0x10) ? 3 : 2;
            std::string text = "MOVE #" + std::to_string(static_cast<uint8_t>(frame[1]) & 0x3F);
            size_t at = 2;
            const char names[4] = {'X', 'Y', 'Z', 'E'};
            for (int axis = 0; axis < 4; ++axis) {
                if (!(header & (0x08 >> axis)) || at + sextets > frame.size()) continue;
                uint32_t value = 0;
                for (int i = 0; i < sextets; ++i) value = (value << 6) | (static_cast<uint8_t>(frame[at++]) & 0x3F);
                const uint32_t sign = 1u << (sextets * 6 - 1);
                const int32_t steps = (value & sign) ? static_cast<int32_t>(value) - static_cast<int32_t>(sign << 1)
                                                     : static_cast<int32_t>(value);
                text += std::string(" ") + names[axis] + std::to_string(steps);
            }
            const auto field = [&frame, &at](int sextets) {
                uint32_t value = 0;
                for (int i = 0; i < sextets && at < frame.size(); ++i) {
                    value = (value << 6) | (static_cast<uint8_t>(frame[at++]) & 0x3F);
                }
                return value;
            };
            if (header & 0x20) {
                text += " V" + std::to_string(field(3) / 64.0).substr(0, 6);
                text += " A" + std::to_string(field(3));
            }
            text += " S" + std::to_string(field(3) / 16.0).substr(0, 6);
            text += " J" + std::to_string(field(2) / 4.0).substr(0, 6);
            return text + " (" + std::to_string(frame.size()) + " byte)";
        }
    }
} // namespace core
