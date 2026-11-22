#pragma once
#include "../../kafka/KafkaConsumerBase.hpp"

namespace connector::events::printer_control {
    class PrinterResumeReceiver : public kafka::KafkaConsumerBase {
    public:
        explicit PrinterResumeReceiver(const kafka::KafkaConfig &config)
            : kafka::KafkaConsumerBase(config, "printer-resume-request") {
        }

    protected:
        std::string getReceiverName() const override {
            return "PrinterResumeReceiver";
        }
    };
}
