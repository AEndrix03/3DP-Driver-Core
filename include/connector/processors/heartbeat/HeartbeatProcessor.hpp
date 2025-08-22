#pragma once

#include "../BaseProcessor.hpp"
#include "../../models/heartbeat/HeartbeatResponse.hpp"
#include "../../events/heartbeat/HeartbeatSender.hpp"
#include "core/DriverInterface.hpp"
#include <memory>

namespace connector::processors::heartbeat {
    class HeartbeatProcessor : public BaseProcessor {
    public:
        HeartbeatProcessor(std::shared_ptr<events::heartbeat::HeartbeatSender> sender,
                           std::shared_ptr<core::DriverInterface> driver,
                           const std::string &driverId);

        void processHeartbeatRequest(const std::string &messageJson, const std::string &key);

        std::string getProcessorName() const override {
            return "HeartbeatProcessor";
        }

        bool isReady() const override {
            return sender_ && sender_->isReady() && driver_ != nullptr;
        }

    private:
        std::shared_ptr<events::heartbeat::HeartbeatSender> sender_;
        std::shared_ptr<core::DriverInterface> driver_;
        std::string driverId_;

        std::string getDriverStatusCode() const;
    };
}
