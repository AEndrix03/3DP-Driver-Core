#pragma once

#include "../BaseReceiver.hpp"

namespace connector::events::printer_command {

    class PrinterCommandReceiver : public BaseReceiver {
    public:
        void startReceiving() override {
            // TODO: Implement Kafka consumer for printer-command-request topic
        }

        void stopReceiving() override {
            // TODO: Stop Kafka consumer
        }

        bool isReceiving() const override {
            // TODO: Check Kafka consumer status
            return false;
        }

        std::string getTopicName() const override {
            return "printer-command-request";
        }

        std::string getReceiverName() const override {
            return "PrinterCommandReceiver";
        }
    };

}