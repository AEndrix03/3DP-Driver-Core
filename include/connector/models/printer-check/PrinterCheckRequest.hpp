#pragma once

#include "../BaseModel.hpp"
#include <string>

namespace connector::models::printer_check {

    /**
     * @brief Printer check request model matching PrinterCheckRequestDto
     */
    class PrinterCheckRequest : public BaseModel {
    public:
        std::string driverId;
        std::string jobId;
        std::string criteria;

        PrinterCheckRequest() = default;

        PrinterCheckRequest(const std::string &driverId, const std::string &jobId, const std::string &criteria)
                : driverId(driverId), jobId(jobId), criteria(criteria) {}

        explicit PrinterCheckRequest(const nlohmann::json &json) { fromJson(json); }

        // BaseModel implementation
        nlohmann::json toJson() const override {
            return nlohmann::json{
                    {"driverId", driverId},
                    {"jobId",    jobId},
                    {"criteria", criteria}
            };
        }

        void fromJson(const nlohmann::json &json) override {
            driverId = json.at("driverId").get<std::string>();
            jobId = json.at("jobId").get<std::string>();
            criteria = json.at("criteria").get<std::string>();
        }

        bool isValid() const override {
            return !driverId.empty() && !jobId.empty();
        }

        std::string getTypeName() const override {
            return "PrinterCheckRequest";
        }
    };

}