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
#include "application/monitor/SystemMonitor.hpp"
#include "core/serial/impl/RealSerialPort.hpp"

ApplicationController::ApplicationController()
        : isRunning_(false),
          initializationComplete_(false) {
    Logger::logInfo("===============================================");
    Logger::logInfo("[ApplicationController] CONSTRUCTING 3DP DRIVER");
    Logger::logInfo("===============================================");
}

ApplicationController::~ApplicationController() {
    Logger::logInfo("[ApplicationController] Destructor called");
    shutdown();
}

bool ApplicationController::initialize() {
    Logger::logInfo("===============================================");
    Logger::logInfo("[ApplicationController] STARTING 3DP DRIVER APPLICATION");
    Logger::logInfo("===============================================");
    Logger::logInfo("[ApplicationController] Version: 2.0.0 - Always Active Controllers");
    Logger::logInfo("[ApplicationController] Build Date: " + std::string(__DATE__) + " " + std::string(__TIME__));

    // Load configuration
    Logger::logInfo("[ApplicationController] Loading Kafka configuration...");
    kafkaConfig_.resolveFromEnvironment();
    kafkaConfig_.printConfig();

    // Initialize components in order with detailed logging
    Logger::logInfo("[ApplicationController] Starting initialization sequence...");

    // Step 1: Hardware
    Logger::logInfo("[ApplicationController] [1/5] Initializing Hardware...");
    if (!initializeHardware()) {
        Logger::logError("[ApplicationController] ✗ Hardware initialization FAILED");
        return false;
    }
    Logger::logInfo("[ApplicationController] ✓ Hardware initialized successfully");

    // Step 2: Translator
    Logger::logInfo("[ApplicationController] [2/5] Initializing GCode Translator...");
    if (!initializeTranslator()) {
        Logger::logError("[ApplicationController] ✗ Translator initialization FAILED");
        return false;
    }
    Logger::logInfo("[ApplicationController] ✓ GCode Translator ready");

    // Step 3: Kafka Controllers
    Logger::logInfo("[ApplicationController] [3/5] Initializing Kafka Controllers...");
    if (!initializeKafkaControllers()) {
        Logger::logWarning("[ApplicationController] ⚠ Kafka initialization partial - continuing in offline mode");
    } else {
        Logger::logInfo("[ApplicationController] ✓ Kafka Controllers initialized");
    }

    // Step 4: Verify Command Queue is running
    Logger::logInfo("[ApplicationController] [4/5] Verifying Command Queue...");
    if (!verifyCommandQueueStatus()) {
        Logger::logError("[ApplicationController] ✗ Command Queue verification FAILED");
        return false;
    }
    Logger::logInfo("[ApplicationController] ✓ Command Queue RUNNING");

    // Step 5: Start System Monitor
    Logger::logInfo("[ApplicationController] [5/5] Starting System Monitor...");
    monitor_ = std::make_unique<SystemMonitor>(
            heartbeatController_,
            printerCommandController_,
            printerCheckController_,
            printerControlController_,
            printer_,
            commandQueue_  // Pass command queue to monitor
    );
    monitor_->start();
    Logger::logInfo("[ApplicationController] ✓ System Monitor ACTIVE");

    // Print initialization summary
    printInitializationSummary();

    initializationComplete_ = true;
    isRunning_ = true;

    Logger::logInfo("===============================================");
    Logger::logInfo("[ApplicationController] SYSTEM READY - WAITING FOR COMMANDS");
    Logger::logInfo("===============================================");

    return true;
}

void ApplicationController::run() {
    Logger::logInfo("[ApplicationController] Application main loop started");
    Logger::logInfo("[ApplicationController] Press Ctrl+C to shutdown gracefully...");

    auto lastHealthCheck = std::chrono::steady_clock::now();
    const auto healthCheckInterval = std::chrono::seconds(30);

    while (isRunning_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Periodic health check
        auto now = std::chrono::steady_clock::now();
        if (now - lastHealthCheck >= healthCheckInterval) {
            performHealthCheck();
            lastHealthCheck = now;
        }

        // Ensure command queue is always running
        if (commandQueue_ && !commandQueue_->isRunning()) {
            Logger::logWarning("[ApplicationController] Command Queue stopped unexpectedly - restarting...");
            commandQueue_->start();
        }
    }

    Logger::logInfo("[ApplicationController] Main loop exited");
}

void ApplicationController::shutdown() {
    if (!isRunning_ && !initializationComplete_) {
        return; // Already shut down or never initialized
    }

    Logger::logInfo("===============================================");
    Logger::logInfo("[ApplicationController] SHUTTING DOWN APPLICATION");
    Logger::logInfo("===============================================");

    isRunning_ = false;

    // Stop components in reverse order
    Logger::logInfo("[ApplicationController] Stopping System Monitor...");
    if (monitor_) {
        monitor_->stop();
        monitor_.reset();
        Logger::logInfo("[ApplicationController] ✓ System Monitor stopped");
    }

    Logger::logInfo("[ApplicationController] Stopping Command Queue...");
    if (commandQueue_) {
        commandQueue_->stop();
        // Wait for queue to finish processing
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        commandQueue_.reset();
        Logger::logInfo("[ApplicationController] ✓ Command Queue stopped");
    }

    Logger::logInfo("[ApplicationController] Stopping Kafka Controllers...");
    stopKafkaControllers();

    Logger::logInfo("[ApplicationController] Shutting down hardware...");
    if (printer_) {
        printer_->shutdown();
        Logger::logInfo("[ApplicationController] ✓ Hardware shutdown complete");
    }

    Logger::logInfo("===============================================");
    Logger::logInfo("[ApplicationController] APPLICATION SHUTDOWN COMPLETE");
    Logger::logInfo("===============================================");
}

bool ApplicationController::initializeHardware() {
    try {
        Logger::logInfo("[ApplicationController] Creating serial port on: " +
                        kafkaConfig_.serialPort + " @ " +
                        std::to_string(kafkaConfig_.serialBaudrate) + " baud");

        serialPort_ = std::make_shared<core::RealSerialPort>(
                kafkaConfig_.serialPort, kafkaConfig_.serialBaudrate
        );

        Logger::logInfo("[ApplicationController] Creating printer interface...");
        printer_ = std::make_shared<core::RealPrinter>(serialPort_);

        Logger::logInfo("[ApplicationController] Creating driver interface...");
        driver_ = std::make_shared<core::DriverInterface>(printer_, serialPort_);

        Logger::logInfo("[ApplicationController] Initializing printer hardware...");
        printer_->initialize();

        Logger::logInfo("[ApplicationController] Hardware initialization complete");
        Logger::logInfo("[ApplicationController]   Port: " + kafkaConfig_.serialPort);
        Logger::logInfo("[ApplicationController]   Baudrate: " + std::to_string(kafkaConfig_.serialBaudrate));

        return true;
    } catch (const std::exception &e) {
        Logger::logError("[ApplicationController] Hardware initialization failed: " + std::string(e.what()));
        return false;
    }
}

bool ApplicationController::initializeTranslator() {
    try {
        Logger::logInfo("[ApplicationController] Creating GCode translator...");
        translator_ = std::make_shared<translator::gcode::GCodeTranslator>(driver_);

        Logger::logInfo("[ApplicationController] Registering GCode dispatchers...");
        initializeDispatchers();

        Logger::logInfo("[ApplicationController] Creating Command Executor Queue...");
        initializeCommandExecutorQueue();

        // Verify queue is running
        if (!commandQueue_ || !commandQueue_->isRunning()) {
            Logger::logError("[ApplicationController] Command Queue failed to start!");
            return false;
        }

        Logger::logInfo("[ApplicationController] GCode translator initialization complete");
        Logger::logInfo("[ApplicationController]   Dispatchers registered: 7");
        Logger::logInfo("[ApplicationController]   Command Queue: RUNNING");

        return true;
    } catch (const std::exception &e) {
        Logger::logError("[ApplicationController] Translator initialization failed: " + std::string(e.what()));
        return false;
    }
}

bool ApplicationController::initializeKafkaControllers() {
    try {
        Logger::logInfo("[ApplicationController] Initializing Kafka Controllers...");

        // Initialize HeartbeatController
        Logger::logInfo("[ApplicationController]   Creating HeartbeatController...");
        heartbeatController_ = std::make_unique<connector::controllers::HeartbeatController>(
                kafkaConfig_, driver_
        );
        heartbeatController_->start();

        // Initialize PrinterCommandController
        Logger::logInfo("[ApplicationController]   Creating PrinterCommandController...");
        printerCommandController_ = std::make_unique<connector::controllers::PrinterCommandController>(
                kafkaConfig_, driver_, commandQueue_
        );
        printerCommandController_->start();

        // Initialize PrinterCheckController
        Logger::logInfo("[ApplicationController]   Creating PrinterCheckController...");
        printerCheckController_ = std::make_unique<connector::controllers::PrinterCheckController>(
                kafkaConfig_, driver_, commandQueue_
        );
        printerCheckController_->start();

        // Initialize PrintJobManager
        Logger::logInfo("[ApplicationController]   Creating PrintJobManager...");
        jobManager_ = std::make_shared<core::print::PrintJobManager>(driver_, commandQueue_);

        // Initialize PrinterControlController
        Logger::logInfo("[ApplicationController]   Creating PrinterControlController...");
        printerControlController_ = std::make_unique<connector::controllers::PrinterControlController>(
                kafkaConfig_, driver_, commandQueue_, jobManager_
        );
        printerControlController_->start();

        // Report status with detailed info
        Logger::logInfo("[ApplicationController] Kafka Controllers Status:");

        if (heartbeatController_ && heartbeatController_->isRunning()) {
            Logger::logInfo("[ApplicationController]   ✓ HeartbeatController: RUNNING");
        } else {
            Logger::logWarning("[ApplicationController]   ⚠ HeartbeatController: OFFLINE");
        }

        if (printerCommandController_ && printerCommandController_->isRunning()) {
            Logger::logInfo("[ApplicationController]   ✓ PrinterCommandController: RUNNING");
        } else {
            Logger::logWarning("[ApplicationController]   ⚠ PrinterCommandController: OFFLINE");
        }

        if (printerCheckController_ && printerCheckController_->isRunning()) {
            Logger::logInfo("[ApplicationController]   ✓ PrinterCheckController: RUNNING");
        } else {
            Logger::logWarning("[ApplicationController]   ⚠ PrinterCheckController: OFFLINE");
        }

        if (printerControlController_ && printerControlController_->isRunning()) {
            Logger::logInfo("[ApplicationController]   ✓ PrinterControlController: RUNNING");
        } else {
            Logger::logWarning("[ApplicationController]   ⚠ PrinterControlController: OFFLINE");
        }

        // Check if at least one controller is running
        bool anyRunning = (heartbeatController_ && heartbeatController_->isRunning()) ||
                          (printerCommandController_ && printerCommandController_->isRunning()) ||
                          (printerCheckController_ && printerCheckController_->isRunning()) ||
                          (printerControlController_ && printerControlController_->isRunning());

        if (!anyRunning) {
            Logger::logWarning("[ApplicationController] No Kafka controllers running - operating in OFFLINE mode");
            Logger::logWarning("[ApplicationController] Commands can still be executed via direct serial connection");
        }

        return true; // Always return true - system can work without Kafka

    } catch (const std::exception &e) {
        Logger::logError("[ApplicationController] Kafka initialization error: " + std::string(e.what()));
        Logger::logWarning("[ApplicationController] Continuing in OFFLINE mode");
        return true; // Still return true to allow offline operation
    }
}

void ApplicationController::initializeDispatchers() {
    Logger::logInfo("[ApplicationController] Registering GCode dispatchers:");

    translator_->registerDispatcher(std::make_unique<translator::gcode::MotionDispatcher>(driver_));
    Logger::logInfo("[ApplicationController]   ✓ MotionDispatcher (G0, G1, G28, etc.)");

    translator_->registerDispatcher(std::make_unique<translator::gcode::SystemDispatcher>(driver_));
    Logger::logInfo("[ApplicationController]   ✓ SystemDispatcher (M24, M25, M112, etc.)");

    translator_->registerDispatcher(std::make_unique<translator::gcode::ExtruderDispatcher>(driver_));
    Logger::logInfo("[ApplicationController]   ✓ ExtruderDispatcher (M82, M83, etc.)");

    translator_->registerDispatcher(std::make_unique<translator::gcode::FanDispatcher>(driver_));
    Logger::logInfo("[ApplicationController]   ✓ FanDispatcher (M106, M107)");

    translator_->registerDispatcher(std::make_unique<translator::gcode::EndstopDispatcher>(driver_));
    Logger::logInfo("[ApplicationController]   ✓ EndstopDispatcher (M119)");

    translator_->registerDispatcher(std::make_unique<translator::gcode::TemperatureDispatcher>(driver_));
    Logger::logInfo("[ApplicationController]   ✓ TemperatureDispatcher (M104, M109, etc.)");

    translator_->registerDispatcher(std::make_unique<translator::gcode::HistoryDispatcher>(driver_));
    Logger::logInfo("[ApplicationController]   ✓ HistoryDispatcher");

    Logger::logInfo("[ApplicationController] All GCode dispatchers registered successfully");
}

void ApplicationController::initializeCommandExecutorQueue() {
    Logger::logInfo("[ApplicationController] Initializing Command Executor Queue...");

    commandQueue_ = std::make_shared<core::CommandExecutorQueue>(translator_);

    // Start the queue immediately
    commandQueue_->start();

    // Wait a moment for initialization
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify it's running
    if (!commandQueue_->isRunning()) {
        Logger::logError("[ApplicationController] CRITICAL: Command Queue failed to start!");
        throw std::runtime_error("Command Queue initialization failed");
    }

    Logger::logInfo("[ApplicationController] Command Executor Queue initialized:");
    Logger::logInfo("[ApplicationController]   Status: RUNNING");
    Logger::logInfo("[ApplicationController]   Max Queue Size: 10000");
    Logger::logInfo("[ApplicationController]   Processing Delay: 10ms");
    Logger::logInfo("[ApplicationController]   Auto-restart: ENABLED");
}

bool ApplicationController::verifyCommandQueueStatus() {
    if (!commandQueue_) {
        Logger::logError("[ApplicationController] Command Queue is null!");
        return false;
    }

    if (!commandQueue_->isRunning()) {
        Logger::logWarning("[ApplicationController] Command Queue not running - attempting to start...");
        commandQueue_->start();

        // Wait a moment and check again
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (!commandQueue_->isRunning()) {
            Logger::logError("[ApplicationController] Failed to start Command Queue!");
            return false;
        }
    }

    Logger::logInfo("[ApplicationController] Command Queue verification passed");
    Logger::logInfo("[ApplicationController]   Queue Status: ACTIVE");

    // Test the queue with a simple command
    Logger::logInfo("[ApplicationController] Testing queue with M115 (firmware info)...");
    commandQueue_->enqueue("M115", 5); // Low priority test command

    return true;
}

void ApplicationController::performHealthCheck() {
    Logger::logInfo("[ApplicationController] Performing health check...");

    // Check Command Queue
    if (commandQueue_) {
        if (!commandQueue_->isRunning()) {
            Logger::logWarning("[ApplicationController] Health Check: Command Queue stopped - restarting!");
            commandQueue_->start();
        }
    }

    // Check Kafka Controllers
    int activeControllers = 0;
    if (heartbeatController_ && heartbeatController_->isRunning()) activeControllers++;
    if (printerCommandController_ && printerCommandController_->isRunning()) activeControllers++;
    if (printerCheckController_ && printerCheckController_->isRunning()) activeControllers++;
    if (printerControlController_ && printerControlController_->isRunning()) activeControllers++;

    Logger::logInfo("[ApplicationController] Health Check: " +
                    std::to_string(activeControllers) + "/4 Kafka controllers active");

    // Check hardware connection
    if (printer_ && printer_->isSystemReady()) {
        Logger::logInfo("[ApplicationController] Health Check: Hardware ready");
    } else {
        Logger::logWarning("[ApplicationController] Health Check: Hardware not ready or disconnected!");
    }
}

void ApplicationController::stopKafkaControllers() {
    if (heartbeatController_) {
        Logger::logInfo("[ApplicationController]   Stopping HeartbeatController...");
        heartbeatController_->stop();
        heartbeatController_.reset();
    }

    if (printerCommandController_) {
        Logger::logInfo("[ApplicationController]   Stopping PrinterCommandController...");
        printerCommandController_->stop();
        printerCommandController_.reset();
    }

    if (printerCheckController_) {
        Logger::logInfo("[ApplicationController]   Stopping PrinterCheckController...");
        printerCheckController_->stop();
        printerCheckController_.reset();
    }

    if (printerControlController_) {
        Logger::logInfo("[ApplicationController]   Stopping PrinterControlController...");
        printerControlController_->stop();
        printerControlController_.reset();
    }

    Logger::logInfo("[ApplicationController] ✓ All Kafka controllers stopped");
}

void ApplicationController::printInitializationSummary() {
    Logger::logInfo("===============================================");
    Logger::logInfo("[ApplicationController] INITIALIZATION SUMMARY");
    Logger::logInfo("===============================================");
    Logger::logInfo("  Hardware:");
    Logger::logInfo("    Serial Port: " + kafkaConfig_.serialPort);
    Logger::logInfo("    Baudrate: " + std::to_string(kafkaConfig_.serialBaudrate));
    Logger::logInfo("    Status: " + std::string(printer_ ? "✓ CONNECTED" : "✗ DISCONNECTED"));

    Logger::logInfo("  GCode System:");
    Logger::logInfo("    Translator: ✓ READY");
    Logger::logInfo("    Dispatchers: ✓ 7 REGISTERED");
    Logger::logInfo("    Command Queue: " +
                    std::string(commandQueue_ && commandQueue_->isRunning() ? "✓ RUNNING" : "✗ STOPPED"));

    Logger::logInfo("  Kafka Controllers:");
    Logger::logInfo("    Heartbeat: " +
                    std::string(heartbeatController_ && heartbeatController_->isRunning() ? "✓ ONLINE" : "⚠ OFFLINE"));
    Logger::logInfo("    Commands: " + std::string(
            printerCommandController_ && printerCommandController_->isRunning() ? "✓ ONLINE" : "⚠ OFFLINE"));
    Logger::logInfo("    Checks: " + std::string(
            printerCheckController_ && printerCheckController_->isRunning() ? "✓ ONLINE" : "⚠ OFFLINE"));
    Logger::logInfo("    Control: " + std::string(
            printerControlController_ && printerControlController_->isRunning() ? "✓ ONLINE" : "⚠ OFFLINE"));

    Logger::logInfo("  System Monitor: " + std::string(monitor_ ? "✓ ACTIVE" : "✗ INACTIVE"));
    Logger::logInfo("===============================================");
}

