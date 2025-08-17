#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace connector::models {

    /**
     * @brief Base interface for all connector models
     */
    class BaseModel {
    public:
        virtual ~BaseModel() = default;

        /**
         * @brief Serialize model to JSON
         */
        virtual nlohmann::json toJson() const = 0;

        /**
         * @brief Deserialize model from JSON
         */
        virtual void fromJson(const nlohmann::json &json) = 0;

        /**
         * @brief Validate model data
         */
        virtual bool isValid() const = 0;

        /**
         * @brief Get model type name
         */
        virtual std::string getTypeName() const = 0;
    };

} // namespace connector::models