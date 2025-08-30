//
// Created by Andrea on 23/08/2025.
//

#pragma once

#include <memory>
#include <thread>
#include <chrono>
#include <atomic>

// Core includes
#include "core/serial/impl/RealSerialPort.hpp"
#include "core/printer/impl/RealPrinter.hpp"
#include "core/DriverInterface.hpp"
#include "core/queue/CommandExecutorQueue.hpp"

// Connector includes
#include "connector/controllers/HeartbeatController.hpp"
#include "connector/controllers/PrinterCommandController.hpp"
#include "connector/controllers/PrinterCheckController.hpp"
#include "connector/controllers/PrinterControlController.hpp"
#include "application/monitor/SystemMonitor.hpp"


/**
 * @class ApplicationController
 * @brief Main application controller for the 3D Printer Driver
 *
 * This class orchestrates the entire 3D printer driver application:
 * - Initializes hardware connections (serial port, printer)
 * - Sets up the GCode translation system with dispatchers
 * - Manages the command execution queue (always active)
 * - Initializes and manages Kafka controllers for remote communication
 * - Monitors system health and ensures components stay active
 *
 * @version 2.0.0 - Always Active Controllers
 * @author Andrea
 */
class ApplicationController {
public:
    /**
     * @brief Constructor - initializes the application controller
     */
    ApplicationController();

    /**
     * @brief Destructor - ensures clean shutdown
     */
    ~ApplicationController();

    /**
     * @brief Initialize all application components
     *
     * Initialization sequence:
     * 1. Hardware (serial port, printer, driver)
     * 2. GCode Translator and dispatchers
     * 3. Command Executor Queue (always running)
     * 4. Kafka Controllers (optional - system works offline)
     * 5. System Monitor
     *
     * @return true if initialization successful, false otherwise
     */
    bool initialize();

    /**
     * @brief Run the main application loop
     *
     * The main loop:
     * - Keeps the application running
     * - Performs periodic health checks
     * - Ensures command queue stays active
     * - Monitors component status
     */
    void run();

    /**
     * @brief Shutdown the application gracefully
     *
     * Stops all components in reverse initialization order
     */
    void shutdown();

private:
    // ========== Hardware Components ==========
    std::shared_ptr<core::RealSerialPort> serialPort_;
    std::shared_ptr<core::RealPrinter> printer_;
    std::shared_ptr<core::DriverInterface> driver_;

    // ========== Translation System ==========
    std::shared_ptr<translator::gcode::GCodeTranslator> translator_;
    std::shared_ptr<core::CommandExecutorQueue> commandQueue_;

    // ========== Kafka Components ==========
    connector::kafka::KafkaConfig kafkaConfig_;
    std::unique_ptr<connector::controllers::HeartbeatController> heartbeatController_;
    std::unique_ptr<connector::controllers::PrinterCommandController> printerCommandController_;
    std::unique_ptr<connector::controllers::PrinterCheckController> printerCheckController_;
    std::unique_ptr<connector::controllers::PrinterControlController> printerControlController_;

    // ========== Print Management ==========
    std::shared_ptr<core::print::PrintJobManager> jobManager_;

    // ========== Monitoring ==========
    std::unique_ptr<SystemMonitor> monitor_;

    // ========== State Management ==========
    std::atomic<bool> isRunning_;
    std::atomic<bool> initializationComplete_;

    // ========== Initialization Methods ==========
    /**
     * @brief Initialize hardware components (serial, printer, driver)
     * @return true if successful, false otherwise
     */
    bool initializeHardware();

    /**
     * @brief Initialize the GCode translator and command queue
     * @return true if successful, false otherwise
     */
    bool initializeTranslator();

    /**
     * @brief Initialize all Kafka controllers
     * @return true if successful (works even if Kafka offline)
     */
    bool initializeKafkaControllers();

    /**
     * @brief Register all GCode command dispatchers
     */
    void initializeDispatchers();

    /**
     * @brief Initialize and start the command executor queue
     * @throws std::runtime_error if queue fails to start
     */
    void initializeCommandExecutorQueue();

    // ========== Verification & Monitoring ==========
    /**
     * @brief Verify that the command queue is running properly
     * @return true if queue is active, false otherwise
     */
    bool verifyCommandQueueStatus();

    /**
     * @brief Perform periodic health check on all components
     *
     * Checks:
     * - Command queue status (restarts if needed)
     * - Kafka controller status
     * - Hardware connection status
     */
    void performHealthCheck();

    /**
     * @brief Stop all Kafka controllers gracefully
     */
    void stopKafkaControllers();

    /**
     * @brief Print detailed initialization summary
     *
     * Shows status of:
     * - Hardware components
     * - GCode system
     * - Kafka controllers
     * - System monitor
     */
    void printInitializationSummary();
};

