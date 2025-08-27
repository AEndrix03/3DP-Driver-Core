//
// Created by Andrea on 23/08/2025.
//

#pragma once

#include "connector/controllers/HeartbeatController.hpp"
#include "connector/controllers/PrinterCommandController.hpp"
#include "connector/controllers/PrinterCheckController.hpp"
#include "core/printer/impl/RealPrinter.hpp"
#include <memory>
#include <thread>
#include <atomic>

#include "connector/controllers/PrinterControlController.hpp"

class SystemMonitor {
public:
    SystemMonitor(std::unique_ptr<connector::controllers::HeartbeatController> &heartbeatController,
                  std::unique_ptr<connector::controllers::PrinterCommandController> &printerCommandController,
                  std::unique_ptr<connector::controllers::PrinterCheckController> &printerCheckController,
                  std::unique_ptr<connector::controllers::PrinterControlController> &printerControlController,
                  std::shared_ptr<core::RealPrinter> printer);

    ~SystemMonitor();

    void start();

    void stop();

    bool isRunning() const;

private:
    std::atomic<bool> running_{false};
    std::thread monitorThread_;

    std::unique_ptr<connector::controllers::HeartbeatController> &heartbeatController_;
    std::unique_ptr<connector::controllers::PrinterCommandController> &printerCommandController_;
    std::unique_ptr<connector::controllers::PrinterCheckController> &printerCheckController_;
    std::unique_ptr<connector::controllers::PrinterControlController> &printerControlController_;
    std::shared_ptr<core::RealPrinter> printer_;

    void monitorLoop();

    void reportKafkaStats() const;
};
