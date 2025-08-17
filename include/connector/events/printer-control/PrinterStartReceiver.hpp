#pragma once

#include "../BaseReceiver.hpp"

namespace connector::events::printer_control {

    class PrinterStartReceiver : public BaseReceiver {
    public:
        void startReceiving() override {
            // TODO: Implement Kafka consumer for printer-start-request topic
        }

        void stopReceiving() override {
            // TODO: Stop Kafka consumer
        }

        bool isReceiving() const override {
            // TODO: Check Kafka consumer status
            return false;
        }

        std::string getTopicName() const override {
            return "printer-start-request";
        }

        std::string getReceiverName() const override {
            return "PrinterStartReceiver";
        }
    };

}