#pragma once

#include "../../kafka/KafkaProducerBase.hpp"

namespace connector::events::printer_check {
    class PrinterCheckSender : public kafka::KafkaProducerBase {
    public:
        explicit PrinterCheckSender(const kafka::KafkaConfig &config);

    protected:
        std::string getSenderName() const override {
            return "PrinterCheckSender";
        }
    };
}
