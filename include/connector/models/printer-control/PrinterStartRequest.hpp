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
        std::string startGCode;
        std::string endGCode;
        std::string gcodeUrl;
        std::string jobId; ///< id del job sul backend: e' quello usato poi per stato, pausa e ripresa

        PrinterStartRequest() = default;

        PrinterStartRequest(const std::string &driverId, const std::string &startGCode,
                            const std::string &endGCode, const std::string &gcodeUrl)
            : driverId(driverId), startGCode(startGCode), endGCode(endGCode), gcodeUrl(gcodeUrl) {
        }

        explicit PrinterStartRequest(const nlohmann::json &json) { fromJson(json); }

        // BaseModel implementation
        nlohmann::json toJson() const override {
            return nlohmann::json{
                {"driverId", driverId},
                {"startGCode", startGCode},
                {"endGCode", endGCode},
                {"gcodeUrl", gcodeUrl},
                {"jobId", jobId}
            };
        }

        void fromJson(const nlohmann::json &json) override {
            driverId = json.at("driverId").get<std::string>();

            // Optional fields - handle null safely
            if (json.contains("startGCode") && !json["startGCode"].is_null()) {
                startGCode = json["startGCode"].get<std::string>();
            }
            if (json.contains("endGCode") && !json["endGCode"].is_null()) {
                endGCode = json["endGCode"].get<std::string>();
            }
            if (json.contains("gcodeUrl") && !json["gcodeUrl"].is_null()) {
                gcodeUrl = json["gcodeUrl"].get<std::string>();
            }
            if (json.contains("jobId") && !json["jobId"].is_null()) {
                jobId = json["jobId"].get<std::string>();
            }
        }

        bool isValid() const override {
            return !driverId.empty() && (!gcodeUrl.empty() || !startGCode.empty());
        }

        std::string getTypeName() const override {
            return "PrinterStartRequest";
        }
    };
}
