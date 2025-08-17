#pragma once

#include "../events/heartbeat/HeartbeatReceiver.hpp"
#include "../events/heartbeat/HeartbeatSender.hpp"
#include "../processors/heartbeat/HeartbeatProcessor.hpp"
#include "../kafka/KafkaConfig.hpp"
#include "core/DriverInterface.hpp"
#include <memory>

namespace connector::controllers {

    class HeartbeatController {
    public:
        HeartbeatController(const kafka::KafkaConfig &config,
                            std::shared_ptr<core::DriverInterface> driver);

        ~HeartbeatController();

        void start();

        void stop();

        bool isRunning() const;

        struct Statistics {
            size_t messagesReceived = 0;
            size_t messagesProcessed = 0;
            size_t messagesSent = 0;
            size_t processingErrors = 0;
        };

        Statistics getStatistics() const;

    private:
        kafka::KafkaConfig config_;
        std::shared_ptr<core::DriverInterface> driver_;

        std::shared_ptr<events::heartbeat::HeartbeatReceiver> receiver_;
        std::shared_ptr<events::heartbeat::HeartbeatSender> sender_;
        std::shared_ptr<processors::heartbeat::HeartbeatProcessor> processor_;

        mutable Statistics stats_;
        bool running_;

        void onMessageReceived(const std::string &message, const std::string &key);
    };

} // namespace connector::controllers