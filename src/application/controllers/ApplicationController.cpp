//
// Created by Andrea on 23/08/2025.
//

#include "application/controllers/ApplicationController.hpp"
#include "logger/Logger.hpp"

// Dispatcher includes
#include "translator/dispatchers/motion/MotionDispatcher.hpp"
#include "translator/dispatchers/system/SystemDispatcher.hpp"
#include "translator/dispatchers/extruder/ExtruderDispatcher.hpp"
#include "translator/dispatchers/fan/FanDispatcher.hpp"
#include "translator/dispatchers/endstop/EndstopDispatcher.hpp"
#include "translator/dispatchers/temperature/TemperatureDispatcher.hpp"
#include "translator/dispatchers/history/HistoryDispatcher.hpp"

ApplicationController::ApplicationController() = default;

ApplicationController::~ApplicationController() {
    shutdown();
}

bool ApplicationController::initialize() {
    Logger::logInfo("Starting 3DP Driver Application...");

    // Load configuration
    kafkaConfig_.resolveFromEnvironment();
    kafkaConfig_.printConfig();

    // Initialize components in order
    if (!initializeHardware()) return false;
    if (!initializeTranslator()) return false;
    if (!initializeKafkaControllers()) return false;

    // Start system monitor
    monitor_ = std::make_unique<SystemMonitor>(heartbeatController_, printer_);
    monitor_->start();

    Logger::logInfo("Application initialized successfully");
    return true;
}

void ApplicationController::run() {
    Logger::logInfo("Application running. Press Ctrl+C to shutdown...");
    // Main application loop - could add periodic tasks here
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        // Add any periodic tasks here if needed
    }
}

void ApplicationController::shutdown() {
    Logger::logInfo("Shutting down application...");

    if (commandQueue_) {
        commandQueue_->stop();
        commandQueue_.reset();
    }

    if (monitor_) {
        monitor_->stop();
        monitor_.reset();
    }

    if (heartbeatController_) {
        heartbeatController_->stop();
        heartbeatController_.reset();
    }

    if (printer_) {
        printer_->shutdown();
    }

    Logger::logInfo("Application shutdown complete");
}

bool ApplicationController::initializeHardware() {
    try {
        serialPort_ = std::make_shared<core::RealSerialPort>(
            kafkaConfig_.serialPort, kafkaConfig_.serialBaudrate
        );
        printer_ = std::make_shared<core::RealPrinter>(serialPort_);
        driver_ = std::make_shared<core::DriverInterface>(printer_, serialPort_);

        printer_->initialize();
        Logger::logInfo("Hardware initialized on: " + kafkaConfig_.serialPort);
        return true;
    } catch (const std::exception &e) {
        Logger::logError("Hardware initialization failed: " + std::string(e.what()));
        return false;
    }
}

bool ApplicationController::initializeTranslator() {
    try {
        // Create shared translator for queue and potential other consumers
        translator_ = std::make_shared<translator::gcode::GCodeTranslator>(driver_);
        initializeDispatchers();
        initializeCommandExecutorQueue();
        Logger::logInfo("GCode translator and command queue initialized");
        return true;
    } catch (const std::exception &e) {
        Logger::logError("Translator initialization failed: " + std::string(e.what()));
        return false;
    }
}

bool ApplicationController::initializeKafkaControllers() {
    try {
        heartbeatController_ = std::make_unique<connector::controllers::HeartbeatController>(
            kafkaConfig_, driver_
        );
        heartbeatController_->start();

        printerCommandController_ = std::make_unique<connector::controllers::PrinterCommandController>(
            kafkaConfig_, driver_, commandQueue_);
        printerCommandController_->start();

        if (heartbeatController_->isRunning()) {
            Logger::logInfo("HeartbeatController started successfully");
        } else {
            Logger::logWarning("HeartbeatController not running (Kafka issues?)");
        }

        if (printerCommandController_->isRunning()) {
            Logger::logInfo("PrinterCommandController started successfully");
        } else {
            Logger::logWarning("PrinterCommandController not running (Kafka issues?)");
        }

        return true;
    } catch (const std::exception &e) {
        Logger::logError("Kafka initialization failed: " + std::string(e.what()));
        heartbeatController_.reset();
        return true; // Continue without Kafka
    }
}

void ApplicationController::initializeDispatchers() {
    translator_->registerDispatcher(std::make_unique<translator::gcode::MotionDispatcher>(driver_));
    translator_->registerDispatcher(std::make_unique<translator::gcode::SystemDispatcher>(driver_));
    translator_->registerDispatcher(std::make_unique<translator::gcode::ExtruderDispatcher>(driver_));
    translator_->registerDispatcher(std::make_unique<translator::gcode::FanDispatcher>(driver_));
    translator_->registerDispatcher(std::make_unique<translator::gcode::EndstopDispatcher>(driver_));
    translator_->registerDispatcher(std::make_unique<translator::gcode::TemperatureDispatcher>(driver_));
    translator_->registerDispatcher(std::make_unique<translator::gcode::HistoryDispatcher>(driver_));

    Logger::logInfo("All GCode dispatchers registered");
}

void ApplicationController::initializeCommandExecutorQueue() {
    commandQueue_ = std::make_unique<core::CommandExecutorQueue>(translator_);
    commandQueue_->start();
    Logger::logInfo("Command executor queue initialized and started");
}
