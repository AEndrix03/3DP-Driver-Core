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
    monitor_ = std::make_unique<SystemMonitor>(
        heartbeatController_,
        printerCommandController_,
        printerCheckController_,
        printerControlController_,
        printer_
    );
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

    if (printerCommandController_) {
        printerCommandController_->stop();
        printerCommandController_.reset();
    }

    if (printerCheckController_) {
        printerCheckController_->stop();
        printerCheckController_.reset();
    }

    if (printerControlController_) {
        printerControlController_->stop();
        printerControlController_.reset();
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
        // Initialize HeartbeatController (existing)
        heartbeatController_ = std::make_unique<connector::controllers::HeartbeatController>(
            kafkaConfig_, driver_
        );
        heartbeatController_->start();

        // Initialize PrinterCommandController
        printerCommandController_ = std::make_unique<connector::controllers::PrinterCommandController>(
            kafkaConfig_, driver_, commandQueue_);
        printerCommandController_->start();

        // Initialize PrinterCheckController
        printerCheckController_ = std::make_unique<connector::controllers::PrinterCheckController>(
            kafkaConfig_, driver_, commandQueue_);
        printerCheckController_->start();

        // Initialize PrintJobManager
        jobManager_ = std::make_shared<core::print::PrintJobManager>(driver_, commandQueue_);

        // Initialize PrinterControlController
        printerControlController_ = std::make_unique<connector::controllers::PrinterControlController>(
            kafkaConfig_, driver_, commandQueue_, jobManager_);
        printerControlController_->start();

        // Report status
        if (heartbeatController_->isRunning()) {
            Logger::logInfo("HeartbeatController started successfully");
        }
        if (printerCommandController_->isRunning()) {
            Logger::logInfo("PrinterCommandController started successfully");
        }
        if (printerCheckController_->isRunning()) {
            Logger::logInfo("PrinterCheckController started successfully");
        }
        if (printerControlController_->isRunning()) {
            Logger::logInfo("PrinterControlController started successfully");
        } else {
            Logger::logWarning("PrinterControlController not running (Kafka issues?)");
        }

        return true;
    } catch (const std::exception &e) {
        Logger::logError("Kafka initialization failed: " + std::string(e.what()));
        heartbeatController_.reset();
        printerCommandController_.reset();
        printerCheckController_.reset();
        return false;
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
