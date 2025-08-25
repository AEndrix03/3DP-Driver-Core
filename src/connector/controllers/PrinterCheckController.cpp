//
// Created by Andrea on 26/08/2025.
//

#include "connector/controllers/PrinterCheckController.hpp"
#include "connector/models/printer-check/PrinterCheckRequest.hpp"
#include "logger/Logger.hpp"
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace connector::controllers {
    PrinterCheckController::PrinterCheckController(const kafka::KafkaConfig &config,
                                                   std::shared_ptr<core::DriverInterface> driver,
                                                   std::shared_ptr<core::CommandExecutorQueue> commandQueue)
        : config_(config), driver_(driver), commandQueue_(commandQueue), running_(false) {
        if (!driver_) {
            throw std::invalid_argument("DriverInterface cannot be null");
        }
        if (!commandQueue_) {
            throw std::invalid_argument("CommandExecutorQueue cannot be null");
        }

        Logger::logInfo("[PrinterCheckController] Initializing for driver: " + config_.driverId);

        try {
            // Create Kafka components
            Logger::logInfo("[PrinterCheckController] Creating Kafka components...");

            receiver_ = std::make_shared<events::printer_check::PrinterCheckReceiver>(config_);
            sender_ = std::make_shared<events::printer_check::PrinterCheckSender>(config_);
            processor_ = std::make_shared<processors::printer_check::PrinterCheckProcessor>(
                sender_, driver_, commandQueue_, config_.driverId);

            // Register message callback
            receiver_->setMessageCallback([this](const std::string &message, const std::string &key) {
                onMessageReceived(message, key);
            });

            Logger::logInfo("[PrinterCheckController] Created successfully for driver: " + config_.driverId);
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCheckController] Failed to initialize: " + std::string(e.what()));
            receiver_.reset();
            sender_.reset();
            processor_.reset();
        }
    }

    PrinterCheckController::~PrinterCheckController() {
        stop();
    }

    void PrinterCheckController::start() {
        if (running_) {
            Logger::logWarning("[PrinterCheckController] Already running");
            return;
        }

        if (!receiver_ || !sender_ || !processor_) {
            Logger::logError("[PrinterCheckController] Cannot start - components not initialized properly");
            return;
        }

        try {
            Logger::logInfo("[PrinterCheckController] Starting Kafka receiver...");
            receiver_->startReceiving();
            running_ = true;
            Logger::logInfo(
                "[PrinterCheckController] Started successfully - listening on: " + receiver_->getTopicName());
        } catch (const std::exception &e) {
            running_ = false;
            Logger::logError("[PrinterCheckController] Failed to start: " + std::string(e.what()));
        }
    }

    void PrinterCheckController::stop() {
        if (!running_) return;
        running_ = false;

        try {
            if (receiver_) {
                Logger::logInfo("[PrinterCheckController] Stopping receiver...");
                receiver_->stopReceiving();
            }
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCheckController] Error stopping receiver: " + std::string(e.what()));
        }

        Logger::logInfo("[PrinterCheckController] Stopped");
    }

    bool PrinterCheckController::isRunning() const {
        return running_ && receiver_ && receiver_->isReceiving();
    }

    PrinterCheckController::Statistics PrinterCheckController::getStatistics() const {
        return stats_;
    }

    void PrinterCheckController::onMessageReceived(const std::string &message, const std::string &key) {
        stats_.messagesReceived++;

        Logger::logInfo(
            "[PrinterCheckController] Received message, key: " + key + ", size: " + std::to_string(message.size()));
        Logger::logInfo("[PrinterCheckController] Raw message content: " + message);

        try {
            // Parse the JSON message
            nlohmann::json json = nlohmann::json::parse(message);
            Logger::logInfo("[PrinterCheckController] Parsed JSON successfully");

            models::printer_check::PrinterCheckRequest request(json);
            Logger::logInfo("[PrinterCheckController] Created PrinterCheckRequest successfully");

            // Validate request
            if (!request.isValid()) {
                Logger::logError("[PrinterCheckController] Invalid request received - driverId: '" +
                                 request.driverId + "', jobId: '" + request.jobId + "'");
                stats_.processingErrors++;
                return;
            }

            // Check if this request is for our driver
            if (request.driverId != config_.driverId) {
                Logger::logInfo("[PrinterCheckController] Request not for this driver (" + request.driverId +
                                " vs " + config_.driverId + "), ignoring");
                return;
            }

            Logger::logInfo("[PrinterCheckController] Processing check request for job: " + request.jobId +
                            " (criteria: '" + request.criteria + "')");

            if (processor_) {
                processor_->processPrinterCheckRequest(request);
                stats_.messagesProcessed++;
                stats_.messagesSent++;
                Logger::logInfo("[PrinterCheckController] Check request processed successfully");
            } else {
                Logger::logWarning("[PrinterCheckController] Processor not available, dropping message");
            }
        } catch (const nlohmann::json::parse_error &e) {
            stats_.processingErrors++;
            Logger::logError("[PrinterCheckController] JSON parse error: " + std::string(e.what()));
            Logger::logError("[PrinterCheckController] Problematic message: " + message);
        } catch (const nlohmann::json::type_error &e) {
            stats_.processingErrors++;
            Logger::logError("[PrinterCheckController] JSON type error: " + std::string(e.what()));
            Logger::logError("[PrinterCheckController] This usually means a field expected to be a string is null");
            Logger::logError("[PrinterCheckController] Problematic message: " + message);
        } catch (const std::exception &e) {
            stats_.processingErrors++;
            Logger::logError("[PrinterCheckController] Processing failed: " + std::string(e.what()));
            Logger::logError("[PrinterCheckController] Message that caused error: " + message);
        }
    }
} // namespace connector::controllers
