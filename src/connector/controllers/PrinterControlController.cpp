//
// Created by Andrea on 27/08/2025.
//

#include "connector/controllers/PrinterControlController.hpp"
#include "connector/models/printer-control/PrinterStartRequest.hpp"
#include "connector/models/printer-control/PrinterStopRequest.hpp"
#include "connector/models/printer-control/PrinterPauseRequest.hpp"
#include "logger/Logger.hpp"
#include <nlohmann/json.hpp>

namespace connector::controllers {
    PrinterControlController::PrinterControlController(
        const kafka::KafkaConfig &config,
        std::shared_ptr<core::DriverInterface> driver,
        std::shared_ptr<core::CommandExecutorQueue> commandQueue,
        std::shared_ptr<core::print::PrintJobManager> jobManager)
        : config_(config), driver_(driver), commandQueue_(commandQueue), jobManager_(jobManager), running_(false) {
        Logger::logInfo("[PrinterControlController] Initializing for driver: " + config_.driverId);

        try {
            // Create receivers
            startReceiver_ = std::make_shared<events::printer_control::PrinterStartReceiver>(config_);
            stopReceiver_ = std::make_shared<events::printer_control::PrinterStopReceiver>(config_);
            pauseReceiver_ = std::make_shared<events::printer_control::PrinterPauseReceiver>(config_);
            // Create processor
            processor_ = std::make_shared<processors::printer_control::PrinterControlProcessor>(
                driver_, commandQueue_, jobManager_);

            // Set callbacks
            startReceiver_->setMessageCallback([this](const std::string &message, const std::string &key) {
                onStartMessageReceived(message, key);
            });
            stopReceiver_->setMessageCallback([this](const std::string &message, const std::string &key) {
                onStopMessageReceived(message, key);
            });
            pauseReceiver_->setMessageCallback([this](const std::string &message, const std::string &key) {
                onPauseMessageReceived(message, key);
            });

            Logger::logInfo("[PrinterControlController] Created successfully");
        } catch (const std::exception &e) {
            Logger::logError("[PrinterControlController] Failed to initialize: " + std::string(e.what()));
        }
    }

    PrinterControlController::~PrinterControlController() {
        stop();
    }

    void PrinterControlController::start() {
        if (running_) {
            Logger::logWarning("[PrinterControlController] Already running");
            return;
        }

        if (!startReceiver_ || !stopReceiver_ || !pauseReceiver_ || !processor_) {
            Logger::logError("[PrinterControlController] Cannot start - components not initialized");
            return;
        }

        try {
            Logger::logInfo("[PrinterControlController] Starting receivers...");
            startReceiver_->startReceiving();
            stopReceiver_->startReceiving();
            pauseReceiver_->startReceiving();

            running_ = true;
            Logger::logInfo("[PrinterControlController] Started successfully");
        } catch (const std::exception &e) {
            running_ = false;
            Logger::logError("[PrinterControlController] Failed to start: " + std::string(e.what()));
        }
    }

    void PrinterControlController::stop() {
        if (!running_) return;
        running_ = false;

        try {
            if (startReceiver_) startReceiver_->stopReceiving();
            if (stopReceiver_) stopReceiver_->stopReceiving();
            if (pauseReceiver_) pauseReceiver_->stopReceiving();
        } catch (const std::exception &e) {
            Logger::logError("[PrinterControlController] Error stopping: " + std::string(e.what()));
        }

        Logger::logInfo("[PrinterControlController] Stopped");
    }

    bool PrinterControlController::isRunning() const {
        return running_ &&
               startReceiver_ && startReceiver_->isReceiving() &&
               stopReceiver_ && stopReceiver_->isReceiving() &&
               pauseReceiver_ && pauseReceiver_->isReceiving();
    }

    PrinterControlController::Statistics PrinterControlController::getStatistics() const {
        return stats_;
    }

    void PrinterControlController::onStartMessageReceived(const std::string &message, const std::string &key) {
        stats_.startRequests++;
        Logger::logInfo("[PrinterControlController] Start message received, key: " + key);

        try {
            nlohmann::json json = nlohmann::json::parse(message);
            models::printer_control::PrinterStartRequest request(json);

            if (!request.isValid()) {
                Logger::logError("[PrinterControlController] Invalid start request");
                stats_.processingErrors++;
                return;
            }

            if (request.driverId != config_.driverId) {
                Logger::logInfo("[PrinterControlController] Start request not for this driver");
                return;
            }

            processor_->processPrinterStartRequest(request);
        } catch (const std::exception &e) {
            stats_.processingErrors++;
            Logger::logError("[PrinterControlController] Start processing failed: " + std::string(e.what()));
        }
    }

    void PrinterControlController::onStopMessageReceived(const std::string &message, const std::string &key) {
        stats_.stopRequests++;
        Logger::logInfo("[PrinterControlController] Stop message received, key: " + key);

        try {
            nlohmann::json json = nlohmann::json::parse(message);
            models::printer_control::PrinterStopRequest request(json);

            if (!request.isValid() || request.driverId != config_.driverId) {
                Logger::logInfo("[PrinterControlController] Stop request not for this driver");
                return;
            }

            processor_->processPrinterStopRequest(request);
        } catch (const std::exception &e) {
            stats_.processingErrors++;
            Logger::logError("[PrinterControlController] Stop processing failed: " + std::string(e.what()));
        }
    }

    void PrinterControlController::onPauseMessageReceived(const std::string &message, const std::string &key) {
        stats_.pauseRequests++;
        Logger::logInfo("[PrinterControlController] Pause message received, key: " + key);

        try {
            nlohmann::json json = nlohmann::json::parse(message);
            models::printer_control::PrinterPauseRequest request(json);

            if (!request.isValid() || request.driverId != config_.driverId) {
                Logger::logInfo("[PrinterControlController] Pause request not for this driver");
                return;
            }

            processor_->processPrinterPauseRequest(request);
        } catch (const std::exception &e) {
            stats_.processingErrors++;
            Logger::logError("[PrinterControlController] Pause processing failed: " + std::string(e.what()));
        }
    }
}
