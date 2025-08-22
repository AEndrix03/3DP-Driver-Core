#pragma once

#include "../../kafka/KafkaConsumerBase.hpp"

namespace connector::events::printer_check {
    class PrinterCheckReceiver : public kafka::KafkaConsumerBase {
    public:
        explicit PrinterCheckReceiver(const kafka::KafkaConfig &config)
            : kafka::KafkaConsumerBase(config, "printer-check-request") {
        }

    protected:
        std::string getReceiverName() const override {
            return "PrinterCheckReceiver";
        }
    };
}
