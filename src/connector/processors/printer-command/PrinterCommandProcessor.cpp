#include "connector/processors/heartbeat/HeartbeatProcessor.hpp"
#include "logger/Logger.hpp"
#include <nlohmann/json.hpp>

namespace connector::processors::printer_command {

    PrinterCommandProcessor::PrinterCommandProcessor(std::shared_ptr<events::printer-command::PrinterCommandSender> sender,
                               std::shared_ptr<translator::gcode::GCodeTranslator> translator,
                                const std::string &driverId)
            : sender_(sender), translator_(translator), driverId_(driverId) {
    }

    void PrinterCommandProcessor::dispatch(const connector::models::printer_command::PrinterCommandRequest &request) {
        try {
            connector::models::printer_command::PrinterCommandRequest _request;
            if (!request.empty()) {
                nlohmann::json json = nlohmann::json::parse(_request);
                request.fromJson(json);
            }

            Logger::logInfo("[PrinterCommandProcessor] Processing command request id: " + _request.requestId);

            try {
                translator->parseLine(_request.command);
            } catch (const GCodeTranslatorInvalidCommandException &e) {
                connector::models::printer_command::PrinterCommandResponse response(driverId_, _request.requestId, false, "GCodeTranslatorInvalidCommandException", e.message);
                return;
            } catch (const GCodeTranslatorUnknownCommandException &e) {
                connector::models::printer_command::PrinterCommandResponse response(driverId_, _request.requestId, false, "GCodeTranslatorUnknownCommandException", e.message);
                return;
            }

            connector::models::printer_command::PrinterCommandResponse response(driverId_, _request.requestId, true);

            if (!response.isValid()) {
                Logger::logError("[PrinterCommandProcessor] Invalid response created");
                return;
            }


            nlohmann::json responseJson = response.toJson();
            std::string responseMessage = responseJson.dump();

            if (sender_->sendMessage(responseMessage, driverId_)) {
                Logger::logInfo("[PrinterCommandProcessor] Command response sent successfully");
            } else {
                Logger::logError("[PrinterCommandProcessor] Failed to send command response");
            }

        } catch (const std::exception &e) {
            Logger::logError("[PrinterCommandProcessor] Processing error: " + std::string(e.what()));

            try {
                connector::models::printer_command::PrinterCommandResponse response(driverId_, _request.requestId, false, "ERROR", e.message);
                nlohmann::json errorJson = errorResponse.toJson();
                sender_->sendMessage(errorJson.dump(), driverId_);
            } catch (...) {
                Logger::logError("[PrinterCommandProcessor] Failed to send error response");
            }
        }
    }

}