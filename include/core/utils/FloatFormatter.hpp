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
        * @brief Formatta un float rimuovendo zeri finali inutili, max default 2 decimali
        */
    inline std::string formatFloat(float value, int precision = DEFAULT_PRECISION) {
        float factor = std::pow(10.0f, precision);
        float rounded = std::round(value * factor) / factor;
        if (rounded == std::floor(rounded)) {
            return std::to_string(static_cast<int>(rounded));
        }

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(precision) << rounded;
        std::string result = oss.str();

        // Rimuovi zeri finali
        size_t end = result.find_last_not_of('0');
        if (end != std::string::npos && result[end] == '.') end--;
        return result.substr(0, end + 1);
    }

    /**
     * @brief Overload per double
     */
    inline std::string formatFloat(double value) {
        return formatFloat(static_cast<float>(value));
    }
} // namespace core::utils
