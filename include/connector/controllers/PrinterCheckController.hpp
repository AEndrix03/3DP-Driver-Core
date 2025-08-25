//
// Created by Andrea on 26/08/2025.
//

#pragma once

#include "../events/printer-check/PrinterCheckReceiver.hpp"
#include "../events/printer-check/PrinterCheckSender.hpp"
#include "../processors/printer-check/PrinterCheckProcessor.hpp"
#include "../kafka/KafkaConfig.hpp"
#include "core/DriverInterface.hpp"
#include "core/queue/CommandExecutorQueue.hpp"
#include <memory>

namespace connector::controllers {
    class PrinterCheckController {
    public:
        PrinterCheckController(const kafka::KafkaConfig &config,
                               std::shared_ptr<core::DriverInterface> driver,
                               std::shared_ptr<core::CommandExecutorQueue> commandQueue);

        ~PrinterCheckController();

        void start();

        void stop();

        bool isRunning() const;

        struct Statistics {
            size_t messagesReceived = 0;
            size_t messagesProcessed = 0;
            size_t messagesSent = 0;
            size_t processingErrors = 0;
        };

        Statistics getStatistics() const;

    private:
        kafka::KafkaConfig config_;
        std::shared_ptr<core::DriverInterface> driver_;
        std::shared_ptr<core::CommandExecutorQueue> commandQueue_;

        std::shared_ptr<events::printer_check::PrinterCheckReceiver> receiver_;
        std::shared_ptr<events::printer_check::PrinterCheckSender> sender_;
        std::shared_ptr<processors::printer_check::PrinterCheckProcessor> processor_;

        mutable Statistics stats_;
        bool running_;

        void onMessageReceived(const std::string &message, const std::string &key);
    };
} // namespace connector::controllers
