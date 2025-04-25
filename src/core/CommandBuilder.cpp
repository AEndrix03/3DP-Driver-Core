//
// Created by redeg on 26/04/2025.
//

#include "core/CommandBuilder.hpp"
#include <sstream>
#include <iomanip>

namespace core {

/**
 * @brief Costruisce una stringa comando completa da numero, categoria, codice e parametri.
 */
    std::string
    CommandBuilder::buildCommand(uint16_t number, char category, int code, const std::vector<std::string> &params) {
        std::ostringstream oss;

        oss << "N" << number << " " << category << code;
        for (const auto &param: params) {
            oss << " " << param;
        }

        std::string rawCommand = oss.str();
        uint8_t checksum = computeChecksum(rawCommand);

        oss << " *" << static_cast<int>(checksum);

        return oss.str();
    }

/**
 * @brief Calcola il checksum XOR dei caratteri di una stringa.
 */
    uint8_t CommandBuilder::computeChecksum(const std::string &data) {
        uint8_t checksum = 0;
        for (char c: data) {
            checksum ^= static_cast<uint8_t>(c);
        }
        return checksum;
    }

} // namespace core
