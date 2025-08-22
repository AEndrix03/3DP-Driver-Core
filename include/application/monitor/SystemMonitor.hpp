//
// Created by Andrea on 23/08/2025.
//

#pragma once

#include "connector/controllers/HeartbeatController.hpp"
#include "core/printer/impl/RealPrinter.hpp"
#include <memory>
#include <thread>
#include <atomic>

class SystemMonitor {
public:
    SystemMonitor(std::unique_ptr<connector::controllers::HeartbeatController> &heartbeatController,
                  std::shared_ptr<core::RealPrinter> printer);

    ~SystemMonitor();

    void start();

    void stop();

    bool isRunning() const;

private:
    std::atomic<bool> running_{false};
    std::thread monitorThread_;

    std::unique_ptr<connector::controllers::HeartbeatController> &heartbeatController_;
    std::shared_ptr<core::RealPrinter> printer_;

    void monitorLoop();

    void checkHardwareStatus();

    void reportKafkaStats();
};
