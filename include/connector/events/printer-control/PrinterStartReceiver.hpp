#pragma once
#include "../../kafka/KafkaConsumerBase.hpp"

namespace connector::events::printer_control {
    class PrinterStartReceiver : public kafka::KafkaConsumerBase {
    public:
        explicit PrinterStartReceiver(const kafka::KafkaConfig &config)
            : kafka::KafkaConsumerBase(config, "printer-start-request") {
        }

    protected:
        std::string getReceiverName() const override {
            return "PrinterStartReceiver";
        }
    };
}
