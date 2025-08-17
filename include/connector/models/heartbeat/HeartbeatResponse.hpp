#pragma once

#include "../BaseModel.hpp"
#include <string>

namespace connector::models::heartbeat {

    /**
     * @brief Heartbeat response model matching PrinterHeartbeatResponseDto
     */
    class HeartbeatResponse : public BaseModel {
    public:
        std::string driverId;
        std::string statusCode;

        HeartbeatResponse() = default;

        HeartbeatResponse(const std::string &driverId, const std::string &statusCode)
                : driverId(driverId), statusCode(statusCode) {}

        explicit HeartbeatResponse(const nlohmann::json &json) { fromJson(json); }

        // BaseModel implementation
        nlohmann::json toJson() const override {
            return nlohmann::json{
                    {"driverId",   driverId},
                    {"statusCode", statusCode}
            };
        }

        void fromJson(const nlohmann::json &json) override {
            driverId = json.at("driverId").get<std::string>();
            statusCode = json.at("statusCode").get<std::string>();
        }

        bool isValid() const override {
            return !driverId.empty() && !statusCode.empty();
        }

        std::string getTypeName() const override {
            return "HeartbeatResponse";
        }
    };

}