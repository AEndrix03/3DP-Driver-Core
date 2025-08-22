#include "connector/controllers/HeartbeatController.hpp"
#include "logger/Logger.hpp"
#include <stdexcept>

namespace connector::controllers {
    HeartbeatController::HeartbeatController(const kafka::KafkaConfig &config,
                                             std::shared_ptr<core::DriverInterface> driver)
        : config_(config), driver_(driver), running_(false) {
        if (!driver_) {
            throw std::invalid_argument("DriverInterface cannot be null");
        }

        Logger::logInfo("[HeartbeatController] Initializing for driver: " + config_.driverId);

        try {
            // Crea i componenti con gestione errori
            Logger::logInfo("[HeartbeatController] Creating Kafka components...");

            receiver_ = std::make_shared<events::heartbeat::HeartbeatReceiver>(config_);
            sender_ = std::make_shared<events::heartbeat::HeartbeatSender>(config_);
            processor_ = std::make_shared<processors::heartbeat::HeartbeatProcessor>(sender_,
                driver_, config_.driverId);

            // Registra il callback per i messaggi
            receiver_->setMessageCallback([this](const std::string &message, const std::string &key) {
                onMessageReceived(message, key);
            });

            Logger::logInfo("[HeartbeatController] Created successfully for driver: " + config_.driverId);
        } catch (const std::exception &e) {
            Logger::logError("[HeartbeatController] Failed to initialize: " + std::string(e.what()));
            // Non rethrow - permetti all'applicazione di continuare senza Kafka
            receiver_.reset();
            sender_.reset();
            processor_.reset();
        }
    }

    HeartbeatController::~HeartbeatController() {
        stop();
    }

    void HeartbeatController::start() {
        if (running_) {
            Logger::logWarning("[HeartbeatController] Already running");
            return;
        }

        if (!receiver_ || !sender_ || !processor_) {
            Logger::logError("[HeartbeatController] Cannot start - components not initialized properly");
            return;
        }

        try {
            Logger::logInfo("[HeartbeatController] Starting Kafka receiver...");
            receiver_->startReceiving();
            running_ = true;
            Logger::logInfo("[HeartbeatController] Started successfully - listening on: " + receiver_->getTopicName());
        } catch (const std::exception &e) {
            running_ = false;
            Logger::logError("[HeartbeatController] Failed to start: " + std::string(e.what()));
            // Non rethrow - continua l'esecuzione senza Kafka
        }
    }

    void HeartbeatController::stop() {
        if (!running_) return;
        running_ = false;

        try {
            if (receiver_) {
                Logger::logInfo("[HeartbeatController] Stopping receiver...");
                receiver_->stopReceiving();
            }
        } catch (const std::exception &e) {
            Logger::logError("[HeartbeatController] Error stopping receiver: " + std::string(e.what()));
        }

        Logger::logInfo("[HeartbeatController] Stopped");
    }

    bool HeartbeatController::isRunning() const {
        return running_ && receiver_ && receiver_->isReceiving();
    }

    HeartbeatController::Statistics HeartbeatController::getStatistics() const {
        return stats_;
    }

    void HeartbeatController::onMessageReceived(const std::string &message, const std::string &key) {
        stats_.messagesReceived++;

        try {
            if (processor_) {
                processor_->processHeartbeatRequest(message, key);
                stats_.messagesProcessed++;
                stats_.messagesSent++;
            } else {
                Logger::logWarning("[HeartbeatController] Processor not available, dropping message");
            }
        } catch (const std::exception &e) {
            stats_.processingErrors++;
            Logger::logError("[HeartbeatController] Processing failed: " + std::string(e.what()));
        }
    }

    void HeartbeatController::printDebugStatus() const {
        Logger::logInfo("[HeartbeatController] Debug Status:");
        Logger::logInfo("  Running: " + std::string(running_ ? "true" : "false"));
        Logger::logInfo("  Receiver active: " + std::string(receiver_ && receiver_->isReceiving() ? "true" : "false"));
        Logger::logInfo("  Sender ready: " + std::string(sender_ && sender_->isReady() ? "true" : "false"));

        auto stats = getStatistics();
        Logger::logInfo("  Messages received: " + std::to_string(stats.messagesReceived));
        Logger::logInfo("  Messages processed: " + std::to_string(stats.messagesProcessed));
        Logger::logInfo("  Processing errors: " + std::to_string(stats.processingErrors));
    }
} // namespace connector::controllers
