//
// Created by Andrea on 23/08/2025.
//

#include "connector/controllers/PrinterCommandController.hpp"
#include "connector/models/printer-command/PrinterCommandRequest.hpp"
#include "logger/Logger.hpp"
#include <stdexcept>
#include <nlohmann/json.hpp>

// Dispatcher includes for translator initialization
#include "translator/dispatchers/motion/MotionDispatcher.hpp"
#include "translator/dispatchers/system/SystemDispatcher.hpp"
#include "translator/dispatchers/extruder/ExtruderDispatcher.hpp"
#include "translator/dispatchers/fan/FanDispatcher.hpp"
#include "translator/dispatchers/endstop/EndstopDispatcher.hpp"
#include "translator/dispatchers/temperature/TemperatureDispatcher.hpp"
#include "translator/dispatchers/history/HistoryDispatcher.hpp"

namespace connector::controllers {
    PrinterCommandController::PrinterCommandController(const kafka::KafkaConfig &config,
                                                       std::shared_ptr<core::DriverInterface> driver)
        : config_(config), driver_(driver), running_(false) {
        if (!driver_) {
            throw std::invalid_argument("DriverInterface cannot be null");
        }

        Logger::logInfo("[PrinterCommandController] Initializing for driver: " + config_.driverId);

        try {
            // Initialize translator first
            translator_ = std::make_shared<translator::gcode::GCodeTranslator>(driver_);
            initializeTranslator();

            // Create Kafka components
            Logger::logInfo("[PrinterCommandController] Creating Kafka components...");

            receiver_ = std::make_shared<events::printer_command::PrinterCommandReceiver>(config_);
            sender_ = std::make_shared<events::printer_command::PrinterCommandSender>(config_);
            processor_ = std::make_shared<processors::printer_command::PrinterCommandProcessor>(
                sender_, translator_, config_.driverId);

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

        try {
            // Parse the JSON message
            nlohmann::json json = nlohmann::json::parse(message);
            models::printer_command::PrinterCommandRequest request(json);

            // Validate request
            if (!request.isValid()) {
                Logger::logError("[PrinterCommandController] Invalid request received");
                stats_.processingErrors++;
                return;
            }

            // Check if this request is for our driver
            if (request.driverId != config_.driverId) {
                Logger::logInfo("[PrinterCommandController] Request not for this driver, ignoring");
                return;
            }

            if (processor_) {
                processor_->dispatch(request);
                stats_.messagesProcessed++;
                stats_.messagesSent++;
            } else {
                Logger::logWarning("[PrinterCommandController] Processor not available, dropping message");
            }
        } catch (const std::exception &e) {
            stats_.processingErrors++;
            Logger::logError("[PrinterCommandController] Processing failed: " + std::string(e.what()));
        }
    }

    void PrinterCommandController::initializeTranslator() {
        if (!translator_) return;

        // Register all dispatchers
        translator_->registerDispatcher(std::make_unique<translator::gcode::MotionDispatcher>(driver_));
        translator_->registerDispatcher(std::make_unique<translator::gcode::SystemDispatcher>(driver_));
        translator_->registerDispatcher(std::make_unique<translator::gcode::ExtruderDispatcher>(driver_));
        translator_->registerDispatcher(std::make_unique<translator::gcode::FanDispatcher>(driver_));
        translator_->registerDispatcher(std::make_unique<translator::gcode::EndstopDispatcher>(driver_));
        translator_->registerDispatcher(std::make_unique<translator::gcode::TemperatureDispatcher>(driver_));
        translator_->registerDispatcher(std::make_unique<translator::gcode::HistoryDispatcher>(driver_));

        Logger::logInfo("[PrinterCommandController] All GCode dispatchers registered");
    }
} // namespace connector::controllers
