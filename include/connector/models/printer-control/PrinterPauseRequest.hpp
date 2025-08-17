#pragma once

#include "../BaseModel.hpp"
#include <string>

namespace connector::models::printer_control {

    /**
     * @brief Printer pause request model matching PrinterPauseRequestDto
     */
    class PrinterPauseRequest : public BaseModel {
    public:
        std::string driverId;

        PrinterPauseRequest() = default;

        explicit PrinterPauseRequest(const std::string &driverId) : driverId(driverId) {}

        explicit PrinterPauseRequest(const nlohmann::json &json) { fromJson(json); }

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
            return "PrinterPauseRequest";
        }
    };

}