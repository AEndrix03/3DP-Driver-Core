#pragma once

#include "../BaseSender.hpp"

namespace connector::events::heartbeat {

    class HeartbeatSender : public BaseSender {
    public:
        bool sendMessage(const std::string &message, const std::string &key = "") override {
            // TODO: Send to printer-heartbeat-response topic via Kafka producer
            (void) message;
            (void) key;
            return false;
        }

        bool isReady() const override {
            // TODO: Check Kafka producer readiness
            return false;
        }

        std::string getTopicName() const override {
            return "printer-heartbeat-response";
        }

        std::string getSenderName() const override {
            return "HeartbeatSender";
        }
    };

}