#pragma once

#include "../BaseSender.hpp"

namespace connector::events::printer_command {

    class PrinterCommandSender : public BaseSender {
    public:
        bool sendMessage(const std::string &message, const std::string &key = "") override {
            // TODO: Send to printer-command-response topic via Kafka producer
            (void) message;
            (void) key;
            return false;
        }

        bool isReady() const override {
            // TODO: Check Kafka producer readiness
            return false;
        }

        std::string getTopicName() const override {
            return "printer-command-response";
        }

        std::string getSenderName() const override {
            return "PrinterCommandSender";
        }
    };

}