#pragma once

#include "../../kafka/KafkaConsumerBase.hpp"

namespace connector::events::heartbeat {

    class HeartbeatReceiver : public kafka::KafkaConsumerBase {
    public:
        explicit HeartbeatReceiver(const kafka::KafkaConfig &config);

    protected:
        std::string getReceiverName() const override {
            return "HeartbeatReceiver";
        }
    };

}