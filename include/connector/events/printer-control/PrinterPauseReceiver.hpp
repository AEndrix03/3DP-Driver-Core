#pragma once
#include "../../kafka/KafkaConsumerBase.hpp"

namespace connector::events::printer_control {
    class PrinterPauseReceiver : public kafka::KafkaConsumerBase {
    public:
        explicit PrinterPauseReceiver(const kafka::KafkaConfig &config)
            : kafka::KafkaConsumerBase(config, "printer-pause-request") {
        }

    protected:
        std::string getReceiverName() const override {
            return "PrinterPauseReceiver";
        }
    };
}
