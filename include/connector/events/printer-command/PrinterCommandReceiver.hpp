#pragma once

#include "../../kafka/KafkaConsumerBase.hpp"

namespace connector::events::printer_command {
    class PrinterCommandReceiver : public kafka::KafkaConsumerBase {
    public:
        explicit PrinterCommandReceiver(const kafka::KafkaConfig &config);

    protected:
        std::string getReceiverName() const override {
            return "PrinterCommandReceiver";
        }
    };
}
