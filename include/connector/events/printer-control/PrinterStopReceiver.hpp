#pragma once
#include "../../kafka/KafkaConsumerBase.hpp"

namespace connector::events::printer_control {
    class PrinterStopReceiver : public kafka::KafkaConsumerBase {
    public:
        explicit PrinterStopReceiver(const kafka::KafkaConfig &config)
            : kafka::KafkaConsumerBase(config, "printer-stop-request") {
        }

    protected:
        std::string getReceiverName() const override {
            return "PrinterStopReceiver";
        }
    };
}
