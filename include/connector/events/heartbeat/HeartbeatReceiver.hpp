#pragma once

#include "../BaseReceiver.hpp"

namespace connector::events::heartbeat {

    class HeartbeatReceiver : public BaseReceiver {
    public:
        void startReceiving() override {
            // TODO: Implement Kafka consumer for printer-heartbeat-request topic
        }

        void stopReceiving() override {
            // TODO: Stop Kafka consumer
        }

        bool isReceiving() const override {
            // TODO: Check Kafka consumer status
            return false;
        }

        std::string getTopicName() const override {
            return "printer-heartbeat-request";
        }

        std::string getReceiverName() const override {
            return "HeartbeatReceiver";
        }
    };

}