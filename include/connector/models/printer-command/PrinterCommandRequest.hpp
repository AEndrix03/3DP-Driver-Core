#pragma once

#include "../BaseModel.hpp"
#include <string>

namespace connector::models::printer_command {

    /**
     * @brief Printer command request model matching PrinterCommandRequestDto
     */
    class PrinterCommandRequest : public BaseModel {
    public:
        std::string requestId;
        std::string driverId;
        std::string command;
        int priority;

        PrinterCommandRequest() : priority(0) {}

        PrinterCommandRequest(const std::string &requestId, const std::string &driverId,
                              const std::string &command, int priority)
                : requestId(requestId), driverId(driverId), command(command), priority(priority) {}

        explicit PrinterCommandRequest(const nlohmann::json &json) { fromJson(json); }

        // BaseModel implementation
        nlohmann::json toJson() const override {
            return nlohmann::json{
                    {"requestId", requestId},
                    {"driverId",  driverId},
                    {"command",   command},
                    {"priority",  priority}
            };
        }

        void fromJson(const nlohmann::json &json) override {
            requestId = json.at("requestId").get<std::string>();
            driverId = json.at("driverId").get<std::string>();
            command = json.at("command").get<std::string>();
            priority = json.at("priority").get<int>();
        }

        bool isValid() const override {
            return !requestId.empty() && !driverId.empty() && !command.empty();
        }

        std::string getTypeName() const override {
            return "PrinterCommandRequest";
        }
    };

} 