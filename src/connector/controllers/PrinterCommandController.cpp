//
// Created by Andrea on 23/08/2025.
//

#include "connector/controllers/PrinterCommandController.hpp"
#include "connector/models/printer-command/PrinterCommandRequest.hpp"
#include "logger/Logger.hpp"
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace connector::controllers {
    PrinterCommandController::PrinterCommandController(const kafka::KafkaConfig &config,
                                                       std::shared_ptr<core::DriverInterface> driver,
                                                       std::shared_ptr<core::CommandExecutorQueue> commandQueue)
            : config_(config), driver_(driver), commandQueue_(commandQueue), running_(false) {
        if (!driver_) {
            throw std::invalid_argument("DriverInterface cannot be null");
        }
        if (!commandQueue_) {
            throw std::invalid_argument("CommandExecutorQueue cannot be null");
        }

        Logger::logInfo("[PrinterCommandController] Initializing for driver: " + config_.driverId);

        try {
            // Create Kafka components
            Logger::logInfo("[PrinterCommandController] Creating Kafka components...");

            receiver_ = std::make_shared<events::printer_command::PrinterCommandReceiver>(config_);
            sender_ = std::make_shared<events::printer_command::PrinterCommandSender>(config_);
            processor_ = std::make_shared<processors::printer_command::PrinterCommandProcessor>(
                    sender_, commandQueue_, config_.driverId);

            // Register message callback
            receiver_->setMessageCallback([this](const std::string &message, const std::string &key) {
                onMessageReceived(message, key);
            });

            Logger::logInfo("[PrinterCommandController] Created successfully for driver: " + config_.driverId);
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCommandController] Failed to initialize: " + std::string(e.what()));
            receiver_.reset();
            sender_.reset();
            processor_.reset();
        }
    }

    PrinterCommandController::~PrinterCommandController() {
        stop();
    }

    void PrinterCommandController::start() {
        if (running_) {
            Logger::logWarning("[PrinterCommandController] Already running");
            return;
        }

        if (!receiver_ || !sender_ || !processor_) {
            Logger::logError("[PrinterCommandController] Cannot start - components not initialized properly");
            return;
        }

        try {
            Logger::logInfo("[PrinterCommandController] Starting Kafka receiver...");
            receiver_->startReceiving();
            running_ = true;
            Logger::logInfo(
                    "[PrinterCommandController] Started successfully - listening on: " + receiver_->getTopicName());
        } catch (const std::exception &e) {
            running_ = false;
            Logger::logError("[PrinterCommandController] Failed to start: " + std::string(e.what()));
        }
    }

    void PrinterCommandController::stop() {
        if (!running_) return;
        running_ = false;

        try {
            if (receiver_) {
                Logger::logInfo("[PrinterCommandController] Stopping receiver...");
                receiver_->stopReceiving();
            }
        } catch (const std::exception &e) {
            Logger::logError("[PrinterCommandController] Error stopping receiver: " + std::string(e.what()));
        }

        Logger::logInfo("[PrinterCommandController] Stopped");
    }

    bool PrinterCommandController::isRunning() const {
        return running_ && receiver_ && receiver_->isReceiving();
    }

    PrinterCommandController::Statistics PrinterCommandController::getStatistics() const {
        return stats_;
    }

    void PrinterCommandController::onMessageReceived(const std::string &message, const std::string &key) {
        stats_.messagesReceived++;

        Logger::logInfo("[PrinterCommandController] Received message, key: " + key +
                        ", size: " + std::to_string(message.size()));

        try {
            // Parse the JSON message
            nlohmann::json json = nlohmann::json::parse(message);
            Logger::logInfo("[PrinterCommandController] Parsed JSON: " + json.dump());

            models::printer_command::PrinterCommandRequest request(json);

            // Validate request
            if (!request.isValid()) {
                Logger::logError("[PrinterCommandController] Invalid request - missing required fields");
                stats_.processingErrors++;
                return;
            }

            // Log request details
            Logger::logInfo("[PrinterCommandController] Request details:");
            Logger::logInfo("  RequestId: " + request.requestId);
            Logger::logInfo("  DriverId: " + request.driverId);
            Logger::logInfo("  Command: " + request.command);
            Logger::logInfo("  Priority: " + std::to_string(request.priority));

            // Check if this request is for our driver
            if (request.driverId != config_.driverId) {
                Logger::logInfo("[PrinterCommandController] Request not for this driver (" +
                                request.driverId + " vs " + config_.driverId + "), ignoring");
                return;
            }

            Logger::logInfo("[PrinterCommandController] Processing command for our driver");

            if (processor_) {
                processor_->dispatch(request);
                stats_.messagesProcessed++;
                stats_.messagesSent++;  // Response will be sent by processor
                Logger::logInfo("[PrinterCommandController] Command dispatched to processor");
            } else {
                Logger::logError("[PrinterCommandController] Processor not available!");
                stats_.processingErrors++;
            }
        } catch (const nlohmann::json::parse_error &e) {
            stats_.processingErrors++;
            Logger::logError("[PrinterCommandController] JSON parse error: " + std::string(e.what()));
            Logger::logError("[PrinterCommandController] Raw message: " + message);
        } catch (const std::exception &e) {
            stats_.processingErrors++;
            Logger::logError("[PrinterCommandController] Processing failed: " + std::string(e.what()));
        }
    }
} // namespace connector::controllers
