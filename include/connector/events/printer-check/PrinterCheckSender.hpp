#pragma once

#include "../../kafka/KafkaProducerBase.hpp"

namespace connector::events::printer_check {
    class PrinterCheckSender : public kafka::KafkaProducerBase {
    public:
        explicit PrinterCheckSender(const kafka::KafkaConfig &config)
            : kafka::KafkaProducerBase(config, "printer-check-response") {
        }

    protected:
        std::string getSenderName() const override {
            return "PrinterCheckSender";
        }
    };
}
