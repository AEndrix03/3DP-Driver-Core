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
        constexpr int32_t WIDE_LIMIT = 131071;    ///< campo a 3 sestetti (18 bit con segno)
        constexpr uint32_t FEED_LIMIT = 262143;   ///< velocita', mm/min (18 bit)
        constexpr size_t MAX_LENGTH = 19;

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
         * @param delta step con segno per X, Y, Z, E (ognuno entro WIDE_LIMIT)
         * @param feedrate mm/min; 0 = non inviata (resta l'ultima)
         */
        inline std::string encode(uint32_t number, const int32_t delta[4], uint32_t feedrate) {
            bool wide = false;
            uint8_t header = 0xC0;
            for (int axis = 0; axis < 4; ++axis) {
                if (delta[axis] == 0) continue;
                header |= static_cast<uint8_t>(0x08 >> axis);
                if (std::abs(delta[axis]) > NARROW_LIMIT) wide = true;
            }
            if (wide) header |= 0x10;
            if (feedrate > 0) header |= 0x20;

            std::string frame;
            frame.push_back(static_cast<char>(header));
            frame.push_back(static_cast<char>(0x80 | (number & 0x3F)));

            const int sextets = wide ? 3 : 2;
            for (int axis = 0; axis < 4; ++axis) {
                if (delta[axis] == 0) continue;
                const uint32_t value = static_cast<uint32_t>(delta[axis]); // complemento a due, troncato sotto
                for (int i = sextets - 1; i >= 0; --i) {
                    frame.push_back(static_cast<char>(0x80 | ((value >> (6 * i)) & 0x3F)));
                }
            }
            if (feedrate > 0) {
                for (int i = 2; i >= 0; --i) {
                    frame.push_back(static_cast<char>(0x80 | ((feedrate >> (6 * i)) & 0x3F)));
                }
            }

            uint16_t crc = 0xFFFF;
            for (const char c: frame) crc = crcUpdate(crc, static_cast<uint8_t>(c));
            frame.push_back(static_cast<char>(0x80 | ((crc >> 6) & 0x3F)));
            frame.push_back(static_cast<char>(0x80 | (crc & 0x3F)));
            return frame;
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
            if ((header & 0x20) && at + 3 <= frame.size()) {
                uint32_t feed = 0;
                for (int i = 0; i < 3; ++i) feed = (feed << 6) | (static_cast<uint8_t>(frame[at++]) & 0x3F);
                text += " F" + std::to_string(feed);
            }
            return text + " (" + std::to_string(frame.size()) + " byte)";
        }
    }
} // namespace core
