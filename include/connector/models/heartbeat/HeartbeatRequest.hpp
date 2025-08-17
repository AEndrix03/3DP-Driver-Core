#pragma once

#include "../BaseModel.hpp"

namespace connector::models::heartbeat {

    /**
     * @brief Heartbeat request model matching PrinterHeartbeatRequestDto
     */
    class HeartbeatRequest : public BaseModel {
    public:
        HeartbeatRequest() = default;

        explicit HeartbeatRequest(const nlohmann::json &json) { fromJson(json); }

        // BaseModel implementation
        nlohmann::json toJson() const override {
            return nlohmann::json::object(); // Empty payload for broadcast requests
        }

        void fromJson(const nlohmann::json &json) override {
            // Heartbeat requests are typically empty broadcast messages
            (void) json; // Suppress unused parameter warning
        }

        bool isValid() const override {
            return true; // Always valid for heartbeat requests
        }

        std::string getTypeName() const override {
            return "HeartbeatRequest";
        }
    };

}