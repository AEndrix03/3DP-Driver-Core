#pragma once

#include "../BaseModel.hpp"
#include <string>

namespace connector::models::printer_check {
    /**
     * @brief Printer check response model matching PrinterCheckResponseDto
     * All fields are strings as per Java implementation
     */
    class PrinterCheckResponse : public BaseModel {
    public:
        // Basic identifiers
        std::string jobId;
        std::string driverId;
        std::string jobStatusCode;
        std::string printerStatusCode;

        // Position data
        std::string xPosition;
        std::string yPosition;
        std::string zPosition;
        std::string ePosition;

        // Movement data
        std::string feed;
        std::string layer;
        std::string layerHeight;

        // Extruder data
        std::string extruderStatus;
        std::string extruderTemp;

        // Bed data
        std::string bedTemp;

        // Fan data
        std::string fanStatus;
        std::string fanSpeed;

        // Command tracking
        std::string commandOffset;
        std::string lastCommand;

        // Performance data
        std::string averageSpeed;

        // Diagnostics
        std::string exceptions;
        std::string logs;

        PrinterCheckResponse() = default;

        explicit PrinterCheckResponse(const nlohmann::json &json) { fromJson(json); }

        // BaseModel implementation
        nlohmann::json toJson() const override {
            return nlohmann::json{
                {"jobId", jobId},
                {"driverId", driverId},
                {"jobStatusCode", jobStatusCode},
                {"printerStatusCode", printerStatusCode},
                {"xPosition", xPosition},
                {"yPosition", yPosition},
                {"zPosition", zPosition},
                {"ePosition", ePosition},
                {"feed", feed},
                {"layer", layer},
                {"layerHeight", layerHeight},
                {"extruderStatus", extruderStatus},
                {"extruderTemp", extruderTemp},
                {"bedTemp", bedTemp},
                {"fanStatus", fanStatus},
                {"fanSpeed", fanSpeed},
                {"commandOffset", commandOffset},
                {"lastCommand", lastCommand},
                {"averageSpeed", averageSpeed},
                {"exceptions", exceptions},
                {"logs", logs}
            };
        }

        void fromJson(const nlohmann::json &json) override {
            // Helper lambda to safely extract string values, handling null
            auto safeGetString = [&json](const std::string &key) -> std::string {
                if (json.contains(key) && !json[key].is_null()) {
                    return json[key].get<std::string>();
                }
                return "";
            };

            // Required fields
            jobId = json.at("jobId").get<std::string>();
            driverId = json.at("driverId").get<std::string>();
            jobStatusCode = json.at("jobStatusCode").get<std::string>();
            printerStatusCode = json.at("printerStatusCode").get<std::string>();

            // Optional fields - handle null safely
            xPosition = safeGetString("xPosition");
            yPosition = safeGetString("yPosition");
            zPosition = safeGetString("zPosition");
            ePosition = safeGetString("ePosition");
            feed = safeGetString("feed");
            layer = safeGetString("layer");
            layerHeight = safeGetString("layerHeight");
            extruderStatus = safeGetString("extruderStatus");
            extruderTemp = safeGetString("extruderTemp");
            bedTemp = safeGetString("bedTemp");
            fanStatus = safeGetString("fanStatus");
            fanSpeed = safeGetString("fanSpeed");
            commandOffset = safeGetString("commandOffset");
            lastCommand = safeGetString("lastCommand");
            averageSpeed = safeGetString("averageSpeed");
            exceptions = safeGetString("exceptions");
            logs = safeGetString("logs");
        }

        bool isValid() const override {
            return !jobId.empty() && !driverId.empty() &&
                   !jobStatusCode.empty() && !printerStatusCode.empty();
        }

        std::string getTypeName() const override {
            return "PrinterCheckResponse";
        }
    };
}
