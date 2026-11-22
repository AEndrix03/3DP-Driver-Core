//
// Created by redeg on 26/04/2025.
//

#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace core::utils {
    constexpr int DEFAULT_PRECISION = 2;

    /**
     * @brief Formatta un numero con al massimo `precision` decimali, senza zeri finali.
     * Lavora in double: nessuna perdita di precisione sulle coordinate.
     */
    inline std::string formatFloat(double value, int precision = DEFAULT_PRECISION) {
        if (!std::isfinite(value)) return "0";

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(precision) << value;
        std::string result = oss.str();

        if (result.find('.') != std::string::npos) {
            size_t end = result.find_last_not_of('0');
            if (result[end] == '.') end--;
            result.erase(end + 1);
        }
        if (result == "-0") result = "0";
        return result;
    }
} // namespace core::utils
