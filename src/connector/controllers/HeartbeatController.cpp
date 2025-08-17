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

        receiver_ = std::make_shared<events::heartbeat::HeartbeatReceiver>(config_);
        sender_ = std::make_shared<events::heartbeat::HeartbeatSender>(config_);
        processor_ = std::make_shared<processors::heartbeat::HeartbeatProcessor>(sender_,
                                                                                 driver_, config_.driverId);

        receiver_->setMessageCallback([this](const std::string &message, const std::string &key) {
            onMessageReceived(message, key);
        });

        Logger::logInfo("[HeartbeatController] Created for driver: " + config_.driverId);
    }

    HeartbeatController::~HeartbeatController() {
        stop();
    }

    void HeartbeatController::start() {
        if (running_) return;

        try {
            receiver_->startReceiving();
            running_ = true;
            Logger::logInfo("[HeartbeatController] Started - listening on: " + receiver_->getTopicName());
        } catch (const std::exception &e) {
            running_ = false;
            Logger::logError("[HeartbeatController] Failed to start: " + std::string(e.what()));
            throw;
        }
    }

    void HeartbeatController::stop() {
        if (!running_) return;
        running_ = false;

        if (receiver_) {
            receiver_->stopReceiving();
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
            processor_->processHeartbeatRequest(message, key);
            stats_.messagesProcessed++;
            stats_.messagesSent++;
        } catch (const std::exception &e) {
            stats_.processingErrors++;
            Logger::logError("[HeartbeatController] Processing failed: " + std::string(e.what()));
        }
    }

} // namespace connector::controllers