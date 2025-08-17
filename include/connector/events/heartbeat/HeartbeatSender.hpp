#pragma once

#include "../../kafka/KafkaProducerBase.hpp"

namespace connector::events::heartbeat {

    class HeartbeatSender : public kafka::KafkaProducerBase {
    public:
        explicit HeartbeatSender(const kafka::KafkaConfig &config);

    protected:
        std::string getSenderName() const override {
            return "HeartbeatSender";
        }
    };

}