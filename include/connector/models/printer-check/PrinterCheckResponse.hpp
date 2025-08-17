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
                    {"jobId",             jobId},
                    {"driverId",          driverId},
                    {"jobStatusCode",     jobStatusCode},
                    {"printerStatusCode", printerStatusCode},
                    {"xPosition",         xPosition},
                    {"yPosition",         yPosition},
                    {"zPosition",         zPosition},
                    {"ePosition",         ePosition},
                    {"feed",              feed},
                    {"layer",             layer},
                    {"layerHeight",       layerHeight},
                    {"extruderStatus",    extruderStatus},
                    {"extruderTemp",      extruderTemp},
                    {"bedTemp",           bedTemp},
                    {"fanStatus",         fanStatus},
                    {"fanSpeed",          fanSpeed},
                    {"commandOffset",     commandOffset},
                    {"lastCommand",       lastCommand},
                    {"averageSpeed",      averageSpeed},
                    {"exceptions",        exceptions},
                    {"logs",              logs}
            };
        }

        void fromJson(const nlohmann::json &json) override {
            jobId = json.at("jobId").get<std::string>();
            driverId = json.at("driverId").get<std::string>();
            jobStatusCode = json.at("jobStatusCode").get<std::string>();
            printerStatusCode = json.at("printerStatusCode").get<std::string>();
            xPosition = json.at("xPosition").get<std::string>();
            yPosition = json.at("yPosition").get<std::string>();
            zPosition = json.at("zPosition").get<std::string>();
            ePosition = json.at("ePosition").get<std::string>();
            feed = json.at("feed").get<std::string>();
            layer = json.at("layer").get<std::string>();
            layerHeight = json.at("layerHeight").get<std::string>();
            extruderStatus = json.at("extruderStatus").get<std::string>();
            extruderTemp = json.at("extruderTemp").get<std::string>();
            bedTemp = json.at("bedTemp").get<std::string>();
            fanStatus = json.at("fanStatus").get<std::string>();
            fanSpeed = json.at("fanSpeed").get<std::string>();
            commandOffset = json.at("commandOffset").get<std::string>();
            lastCommand = json.at("lastCommand").get<std::string>();
            averageSpeed = json.at("averageSpeed").get<std::string>();
            exceptions = json.at("exceptions").get<std::string>();
            logs = json.at("logs").get<std::string>();
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