#include "connector/events/heartbeat/HeartbeatSender.hpp"

namespace connector::events::heartbeat {

    HeartbeatSender::HeartbeatSender(const kafka::KafkaConfig &config)
            : kafka::KafkaProducerBase(config, "printer-heartbeat-response") {
    }

}