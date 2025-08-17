#pragma once

#include "../BaseModel.hpp"
#include <string>

namespace connector::models::printer_control {

    /**
     * @brief Printer start request model matching PrinterStartRequestDto
     */
    class PrinterStartRequest : public BaseModel {
    public:
        std::string driverId;

        PrinterStartRequest() = default;

        explicit PrinterStartRequest(const std::string &driverId) : driverId(driverId) {}

        explicit PrinterStartRequest(const nlohmann::json &json) { fromJson(json); }

        // BaseModel implementation
        nlohmann::json toJson() const override {
            return nlohmann::json{
                    {"driverId", driverId}
            };
        }

        void fromJson(const nlohmann::json &json) override {
            driverId = json.at("driverId").get<std::string>();
        }

        bool isValid() const override {
            return !driverId.empty();
        }

        std::string getTypeName() const override {
            return "PrinterStartRequest";
        }
    };

} 