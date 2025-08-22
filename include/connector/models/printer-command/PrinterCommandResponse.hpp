#pragma once

#include "../BaseModel.hpp"
#include <string>

namespace connector::models::printer_command {

    /**
     * @brief Printer printer-command response model matching PrinterCommandResponseDto
     */
    class PrinterCommandResponse : public BaseModel {
    public:
        std::string driverId;
        std::string requestId;
        bool ok;
        std::string exception;
        std::string info;

        PrinterCommandResponse() : ok(false) {}

        PrinterCommandResponse(const std::string &driverId, const std::string &requestId, bool ok,
                               const std::string &exception = "", const std::string &info = "")
                : driverId(driverId), requestId(requestId), ok(ok), exception(exception), info(info) {}

        explicit PrinterCommandResponse(const nlohmann::json &json) { fromJson(json); }

        // BaseModel implementation
        nlohmann::json toJson() const override {
            return nlohmann::json{
                    {"driverId",  driverId},
                    {"requestId", requestId},
                    {"ok",        ok},
                    {"exception", exception},
                    {"info",      info}
            };
        }

        void fromJson(const nlohmann::json &json) override {
            driverId = json.at("driverId").get<std::string>();
            requestId = json.at("requestId").get<std::string>();
            ok = json.at("ok").get<bool>();

            // Optional fields
            if (json.contains("exception") && !json["exception"].is_null()) {
                exception = json["exception"].get<std::string>();
            }
            if (json.contains("info") && !json["info"].is_null()) {
                info = json["info"].get<std::string>();
            }
        }

        bool isValid() const override {
            return !driverId.empty() && !requestId.empty();
        }

        std::string getTypeName() const override {
            return "PrinterCommandResponse";
        }
    };

}