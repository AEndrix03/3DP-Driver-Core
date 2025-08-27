//
// Created by Andrea on 23/08/2025.
//

#pragma once

#include "core/DriverInterface.hpp"
#include "core/serial/impl/RealSerialPort.hpp"
#include "core/printer/impl/RealPrinter.hpp"
#include "core/printer/job/PrintJobManager.hpp"
#include "connector/controllers/HeartbeatController.hpp"
#include "connector/controllers/PrinterCommandController.hpp"
#include "connector/controllers/PrinterCheckController.hpp"
#include "connector/controllers/PrinterControlController.hpp"
#include "connector/kafka/KafkaConfig.hpp"
#include "translator/GCodeTranslator.hpp"
#include "core/queue/CommandExecutorQueue.hpp"
#include "../monitor/SystemMonitor.hpp"
#include <memory>


class ApplicationController {
public:
    ApplicationController();

    ~ApplicationController();

    bool initialize();

    void run();

    void shutdown();

private:
    // Core components
    std::shared_ptr<core::RealSerialPort> serialPort_;
    std::shared_ptr<core::RealPrinter> printer_;
    std::shared_ptr<core::DriverInterface> driver_;

    // Kafka components
    connector::kafka::KafkaConfig kafkaConfig_;
    std::unique_ptr<connector::controllers::HeartbeatController> heartbeatController_;
    std::unique_ptr<connector::controllers::PrinterCommandController> printerCommandController_;
    std::unique_ptr<connector::controllers::PrinterCheckController> printerCheckController_;
    std::unique_ptr<connector::controllers::PrinterControlController> printerControlController_;
    std::shared_ptr<core::print::PrintJobManager> jobManager_;

    // GCode translator (shared for queue and controllers)
    std::shared_ptr<translator::gcode::GCodeTranslator> translator_;

    // Command Executor Queue
    std::shared_ptr<core::CommandExecutorQueue> commandQueue_;

    // System monitoring
    std::unique_ptr<SystemMonitor> monitor_;

    // Internal methods
    bool initializeHardware();

    bool initializeTranslator();

    bool initializeKafkaControllers();

    void initializeDispatchers();

    void initializeCommandExecutorQueue();
};
