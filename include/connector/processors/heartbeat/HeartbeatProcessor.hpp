#pragma once

#include "../BaseProcessor.hpp"
#include "../../models/heartbeat/HeartbeatRequest.hpp"
#include "../../models/heartbeat/HeartbeatResponse.hpp"

namespace connector::processors::heartbeat {

    class HeartbeatProcessor : public BaseProcessor {
    public:
        void processHeartbeatRequest(const connector::models::heartbeat::HeartbeatRequest &request) {
            // TODO: Process heartbeat request
            // TODO: Get current driver status
            // TODO: Create HeartbeatResponse
            // TODO: Call HeartbeatSender to send response
            (void) request;
        }

        std::string getProcessorName() const override {
            return "HeartbeatProcessor";
        }

        bool isReady() const override {
            // TODO: Check if processor dependencies are ready
            return false;
        }
    };

}