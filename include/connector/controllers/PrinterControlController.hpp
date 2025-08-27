//
// Created by Andrea on 27/08/2025.
//

#pragma once

#include "../events/printer-control/PrinterStartReceiver.hpp"
#include "../events/printer-control/PrinterStopReceiver.hpp"
#include "../events/printer-control/PrinterPauseReceiver.hpp"
#include "../processors/printer-control/PrinterControlProcessor.hpp"
#include "../kafka/KafkaConfig.hpp"
#include "core/DriverInterface.hpp"
#include "core/queue/CommandExecutorQueue.hpp"
#include "core/printer/job/PrintJobManager.hpp"
#include <memory>

namespace connector::controllers {
    class PrinterControlController {
    public:
        PrinterControlController(const kafka::KafkaConfig &config,
                                 std::shared_ptr<core::DriverInterface> driver,
                                 std::shared_ptr<core::CommandExecutorQueue> commandQueue,
                                 std::shared_ptr<core::print::PrintJobManager> jobManager);

        ~PrinterControlController();

        void start();

        void stop();

        bool isRunning() const;

        struct Statistics {
            size_t startRequests = 0;
            size_t stopRequests = 0;
            size_t pauseRequests = 0;
            size_t processingErrors = 0;
        };

        Statistics getStatistics() const;

    private:
        kafka::KafkaConfig config_;
        std::shared_ptr<core::DriverInterface> driver_;
        std::shared_ptr<core::CommandExecutorQueue> commandQueue_;
        std::shared_ptr<core::print::PrintJobManager> jobManager_;

        std::shared_ptr<events::printer_control::PrinterStartReceiver> startReceiver_;
        std::shared_ptr<events::printer_control::PrinterStopReceiver> stopReceiver_;
        std::shared_ptr<events::printer_control::PrinterPauseReceiver> pauseReceiver_;
        std::shared_ptr<processors::printer_control::PrinterControlProcessor> processor_;

        mutable Statistics stats_;
        bool running_;

        void onStartMessageReceived(const std::string &message, const std::string &key);

        void onStopMessageReceived(const std::string &message, const std::string &key);

        void onPauseMessageReceived(const std::string &message, const std::string &key);
    };
}
