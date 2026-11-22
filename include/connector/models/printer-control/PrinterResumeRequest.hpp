#pragma once

#include "../BaseModel.hpp"
#include <string>

namespace connector::models::printer_control {

    /**
     * @brief Printer resume request model matching PrinterResumeRequestDto
     *
     * Riprende un job in pausa oppure, se il job si era interrotto (guasto,
     * riavvio del firmware o del driver), lo fa ripartire dal suo checkpoint.
     */
    class PrinterResumeRequest : public BaseModel {
    public:
        std::string driverId;
        std::string jobId; ///< facoltativo: senza, viene ripreso il job in pausa o il checkpoint piu' recente

        PrinterResumeRequest() = default;

        explicit PrinterResumeRequest(const std::string &driverId, const std::string &jobId = "")
            : driverId(driverId), jobId(jobId) {}

        explicit PrinterResumeRequest(const nlohmann::json &json) { fromJson(json); }

        // BaseModel implementation
        nlohmann::json toJson() const override {
            return nlohmann::json{
                    {"driverId", driverId},
                    {"jobId",    jobId}
            };
        }

        void fromJson(const nlohmann::json &json) override {
            driverId = json.at("driverId").get<std::string>();
            if (json.contains("jobId") && !json["jobId"].is_null()) {
                jobId = json["jobId"].get<std::string>();
            }
        }

        bool isValid() const override {
            return !driverId.empty();
        }

        std::string getTypeName() const override {
            return "PrinterResumeRequest";
        }
    };

}
