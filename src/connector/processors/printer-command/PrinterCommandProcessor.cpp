#include "connector/processors/printer-command/PrinterCommandProcessor.hpp"
#include "connector/events/printer-command/PrinterCommandSender.hpp"
#include "connector/models/printer-command/PrinterCommandRequest.hpp"
#include "connector/models/printer-command/PrinterCommandResponse.hpp"

#include "logger/Logger.hpp"
#include <nlohmann/json.hpp>

#include "translator/GCodeTranslator.hpp"
#include "translator/exceptions/GCodeTranslatorInvalidCommandException.hpp"
#include "translator/exceptions/GCodeTranslatorUnknownCommandException.hpp"

namespace connector::processors::printer_command {
    PrinterCommandProcessor::PrinterCommandProcessor(
        std::shared_ptr<events::printer_command::PrinterCommandSender> sender,
        std::shared_ptr<translator::gcode::GCodeTranslator> translator,
        const std::string &driverId)
        : sender_(sender), translator_(translator), driverId_(driverId) {
    }

    void PrinterCommandProcessor::dispatch(const connector::models::printer_command::PrinterCommandRequest &request) {
        Logger::logInfo("[PrinterCommandProcessor] Processing command request id: " + request.requestId);
        connector::models::printer_command::PrinterCommandResponse response;
        try {
            // Validate the request
            if (!request.isValid()) {
                Logger::logError("[PrinterCommandProcessor] Invalid request received");
                response = connector::models::printer_command::PrinterCommandResponse(
                    driverId_, request.requestId, false, "InvalidRequest", "Request validation failed");
            } else {
                // Process the G-code command through translator
                Logger::logInfo("[PrinterCommandProcessor] Executing command: " + request.command);
                translator_->parseLine(request.command);
                // If we reach here, command was successful
                response = connector::models::printer_command::PrinterCommandResponse(
                    driverId_, request.requestId, true, "", "Command executed successfully");
                Logger::logInfo("[PrinterCommandProcessor] Command executed successfully: " + request.requestId);
            }
        } catch (const GCodeTranslatorInvalidCommandException &e) {
            Logger::logError("[PrinterCommandProcessor] Invalid G-code command: " + std::string(e.what()));
            response = connector::models::printer_command::PrinterCommandResponse(
                driverId_, request.requestId, false, "GCodeTranslatorInvalidCommandException", e.what());
        } catch (const GCodeTranslatorUnknownCommandException &e) {
            Logger::logError("[PrinterCommandProcessor] Unknown G-code command: " + std::string(e.what()));
            response = connector::models::printer_command::PrinterCommandResponse(
                driverId_, request.requestId, false, "GCodeTranslatorUnknownCommandException", e.what());
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCommandProcessor] Unexpected error: " + std::string(e.what()));
            response = connector::models::printer_command::PrinterCommandResponse(
                driverId_, request.requestId, false, "UnexpectedException", e.what());
        }

        // Send response
        try {
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
            Logger::logError("[PrinterCommandProcessor] Failed to send response: " + std::string(e.what()));
        }
    }
}
