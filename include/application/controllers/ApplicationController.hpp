//
// Created by Andrea on 23/08/2025.
//

#pragma once

#include "core/DriverInterface.hpp"
#include "core/serial/impl/RealSerialPort.hpp"
#include "core/printer/impl/RealPrinter.hpp"
#include "connector/controllers/HeartbeatController.hpp"
#include "connector/kafka/KafkaConfig.hpp"
#include "translator/GCodeTranslator.hpp"
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

    // GCode translator
    std::unique_ptr<translator::gcode::GCodeTranslator> translator_;

    // System monitoring
    std::unique_ptr<SystemMonitor> monitor_;

    // Internal methods
    bool initializeHardware();

    bool initializeTranslator();

    bool initializeKafkaControllers();

    void initializeDispatchers();
};
