#include "connector/events/heartbeat/HeartbeatReceiver.hpp"

namespace connector::events::heartbeat {

    HeartbeatReceiver::HeartbeatReceiver(const kafka::KafkaConfig &config)
            : kafka::KafkaConsumerBase(config, "printer-heartbeat-request") {
    }

}