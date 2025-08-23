//
// Created by Andrea on 23/08/2025.
//

#pragma once

#include "../events/printer-command/PrinterCommandReceiver.hpp"
#include "../events/printer-command/PrinterCommandSender.hpp"
#include "../processors/printer-command/PrinterCommandProcessor.hpp"
#include "../kafka/KafkaConfig.hpp"
#include "core/DriverInterface.hpp"
#include "core/queue/CommandExecutorQueue.hpp"
#include <memory>

namespace connector::controllers {
    class PrinterCommandController {
    public:
        PrinterCommandController(const kafka::KafkaConfig &config,
                                 std::shared_ptr<core::DriverInterface> driver,
                                 std::shared_ptr<core::CommandExecutorQueue> commandQueue);

        ~PrinterCommandController();

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

        std::shared_ptr<events::printer_command::PrinterCommandReceiver> receiver_;
        std::shared_ptr<events::printer_command::PrinterCommandSender> sender_;
        std::shared_ptr<processors::printer_command::PrinterCommandProcessor> processor_;

        mutable Statistics stats_;
        bool running_;

        void onMessageReceived(const std::string &message, const std::string &key);
    };
} // namespace connector::controllers
