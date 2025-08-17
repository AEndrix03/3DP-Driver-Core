#include "connector/processors/heartbeat/HeartbeatProcessor.hpp"
#include "logger/Logger.hpp"
#include <nlohmann/json.hpp>

namespace connector::processors::heartbeat {

    HeartbeatProcessor::HeartbeatProcessor(std::shared_ptr<events::heartbeat::HeartbeatSender> sender,
                                           std::shared_ptr<core::DriverInterface> driver,
                                           const std::string &driverId)
            : sender_(sender), driver_(driver), driverId_(driverId) {
    }

    void HeartbeatProcessor::processHeartbeatRequest(const std::string &messageJson, const std::string &key) {
        Logger::logInfo("[HeartbeatProcessor] Processing heartbeat request from key: " + key);

        try {
            // Parse the heartbeat request
            models::heartbeat::HeartbeatRequest request;
            if (!messageJson.empty()) {
                nlohmann::json json = nlohmann::json::parse(messageJson);
                request.fromJson(json);
            }

            // Get current driver status
            std::string statusCode = getDriverStatusCode();

            // Create response
            models::heartbeat::HeartbeatResponse response(driverId_, statusCode);

            // Validate response
            if (!response.isValid()) {
                Logger::logError("[HeartbeatProcessor] Invalid response created");
                return;
            }

            // Convert to JSON and send
            nlohmann::json responseJson = response.toJson();
            std::string responseMessage = responseJson.dump();

            if (sender_->sendMessage(responseMessage, driverId_)) {
                Logger::logInfo("[HeartbeatProcessor] Heartbeat response sent successfully");
            } else {
                Logger::logError("[HeartbeatProcessor] Failed to send heartbeat response");
            }

        } catch (const std::exception &e) {
            Logger::logError("[HeartbeatProcessor] Processing error: " + std::string(e.what()));

            // Send error response
            try {
                models::heartbeat::HeartbeatResponse errorResponse(driverId_, "ERROR");
                nlohmann::json errorJson = errorResponse.toJson();
                sender_->sendMessage(errorJson.dump(), driverId_);
            } catch (...) {
                Logger::logError("[HeartbeatProcessor] Failed to send error response");
            }
        }
    }

    std::string HeartbeatProcessor::getDriverStatusCode() const {
        if (!driver_) {
            return "UNK";
        }

        switch (driver_->getState()) {
            case core::PrintState::Idle:
                return "IDL";
            case core::PrintState::Running:
                return "RUN";
            case core::PrintState::Paused:
                return "PAU";
            case core::PrintState::Completed:
                return "CMP";
            case core::PrintState::Error:
                return "ERR";
            default:
                return "UNK";
        }
    }

}