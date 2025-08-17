#pragma once

#include "../BaseReceiver.hpp"

namespace connector::events::printer_control {

    class PrinterStopReceiver : public BaseReceiver {
    public:
        void startReceiving() override {
            // TODO: Implement Kafka consumer for printer-stop-request topic
        }

        void stopReceiving() override {
            // TODO: Stop Kafka consumer
        }

        bool isReceiving() const override {
            // TODO: Check Kafka consumer status
            return false;
        }

        std::string getTopicName() const override {
            return "printer-stop-request";
        }

        std::string getReceiverName() const override {
            return "PrinterStopReceiver";
        }
    };

}