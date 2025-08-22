#pragma once

#include "../../kafka/KafkaProducerBase.hpp"

namespace connector::events::printer_command {

    class PrinterCommandSender : public kafka::KafkaProducerBase {
    public:
        explicit PrinterCommandSender(const kafka::KafkaConfig &config);

    protected:
        std::string getSenderName() const override {
            return "PrinterCommandSender";
        }
    };

}