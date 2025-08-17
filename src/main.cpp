#include "core/DriverInterface.hpp"
#include "core/serial/impl/RealSerialPort.hpp"
#include "core/printer/impl/RealPrinter.hpp"
#include "core/printer/Printer.hpp"
#include "logger/Logger.hpp"

#include "translator/GCodeTranslator.hpp"
#include "translator/dispatchers/motion/MotionDispatcher.hpp"
#include "translator/dispatchers/system/SystemDispatcher.hpp"
#include "translator/dispatchers/extruder/ExtruderDispatcher.hpp"
#include "translator/dispatchers/fan/FanDispatcher.hpp"
#include "translator/dispatchers/endstop/EndstopDispatcher.hpp"
#include "translator/dispatchers/temperature/TemperatureDispatcher.hpp"
#include "translator/dispatchers/history/HistoryDispatcher.hpp"

#include "connector/controllers/HeartbeatController.hpp"

#include <iostream>
#include <memory>
#include <atomic>
#include <csignal>
#include <condition_variable>
#include <mutex>

std::atomic<bool> running{true};
std::condition_variable shutdownCondition;
std::mutex shutdownMutex;

void handleSignal(int signal) {
    Logger::logInfo("Received shutdown signal: " + std::to_string(signal));
    running = false;
    shutdownCondition.notify_all();
}

void initializeDispatchers(translator::gcode::GCodeTranslator &translator) {
    const auto driver = translator.getDriver();

    translator.registerDispatcher(std::make_unique<translator::gcode::MotionDispatcher>(driver));
    translator.registerDispatcher(std::make_unique<translator::gcode::SystemDispatcher>(driver));
    translator.registerDispatcher(std::make_unique<translator::gcode::ExtruderDispatcher>(driver));
    translator.registerDispatcher(std::make_unique<translator::gcode::FanDispatcher>(driver));
    translator.registerDispatcher(std::make_unique<translator::gcode::EndstopDispatcher>(driver));
    translator.registerDispatcher(std::make_unique<translator::gcode::TemperatureDispatcher>(driver));
    translator.registerDispatcher(std::make_unique<translator::gcode::HistoryDispatcher>(driver));

    Logger::logInfo("All GCode dispatchers registered");
}

connector::kafka::KafkaConfig createKafkaConfig() {
    connector::kafka::KafkaConfig config;

    // TODO: Load from config file or environment
    config.brokers = "localhost:9092";
    config.driverId = "driver_001";
    config.consumerGroupId = "printer-driver_001";

    return config;
}

int main() {
    try {
        // Initialize logger and signal handlers
        Logger::init();
        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);

        Logger::logInfo("Starting 3DP Driver and Translator...");

        // Initialize hardware components
        std::string portName = "COM4"; // TODO: Load from config
        auto serialPort = std::make_shared<core::RealSerialPort>(portName, 115200);
        auto printer = std::make_shared<core::RealPrinter>(serialPort);
        auto driver = std::make_shared<core::DriverInterface>(printer, serialPort);

        printer->initialize();
        Logger::logInfo("Hardware initialized on port: " + portName);

        // Initialize GCode translator
        translator::gcode::GCodeTranslator translator(driver);
        initializeDispatchers(translator);

        // Initialize and start HeartbeatController
        auto kafkaConfig = createKafkaConfig();
        auto heartbeatController = std::make_unique<connector::controllers::HeartbeatController>(
                kafkaConfig, driver
        );
        heartbeatController->start();
        Logger::logInfo("HeartbeatController started");

        // Initialize and start general connector
        /*auto connector = connector::controllers::HeartbeatController;
        connector->start();
        Logger::logInfo("Connector started");*/

        Logger::logInfo("All systems running. Waiting for shutdown signal...");

        // Wait for shutdown signal (event-driven, no polling)
        {
            std::unique_lock<std::mutex> lock(shutdownMutex);
            shutdownCondition.wait(lock, [] { return !running.load(); });
        }

        // Graceful shutdown
        Logger::logInfo("Shutting down...");

        heartbeatController->stop();
        //connector->stop();
        printer->shutdown();

        Logger::logInfo("Shutdown complete");

    } catch (const std::exception &ex) {
        Logger::logError("[Fatal Error]: " + std::string(ex.what()));
        return 1;
    }

    return 0;
}