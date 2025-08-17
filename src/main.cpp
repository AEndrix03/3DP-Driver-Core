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

    config.resolveFromEnvironment();
    config.printConfig();

    return config;
}

int main() {
    try {
        // Initialize logger and signal handlers
        Logger::init();
        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);

        Logger::logInfo("Starting 3DP Driver and Translator...");

        // Create and resolve config
        auto kafkaConfig = createKafkaConfig();

        // Initialize hardware components using resolved config
        auto serialPort = std::make_shared<core::RealSerialPort>(
                kafkaConfig.serialPort,
                kafkaConfig.serialBaudrate
        );
        auto printer = std::make_shared<core::RealPrinter>(serialPort);
        auto driver = std::make_shared<core::DriverInterface>(printer, serialPort);

        printer->initialize();
        Logger::logInfo("Hardware initialized on port: " + kafkaConfig.serialPort);

        // Initialize GCode translator
        translator::gcode::GCodeTranslator translator(driver);
        initializeDispatchers(translator);

        // Initialize and start HeartbeatController
        Logger::logInfo("Initializing HeartbeatController...");
        std::unique_ptr<connector::controllers::HeartbeatController> heartbeatController;

        try {
            heartbeatController = std::make_unique<connector::controllers::HeartbeatController>(
                    kafkaConfig, driver
            );
            heartbeatController->start();

            if (heartbeatController->isRunning()) {
                Logger::logInfo("HeartbeatController started successfully");
            } else {
                Logger::logWarning("HeartbeatController created but not running (Kafka connection issues?)");
            }
        } catch (const std::exception &e) {
            Logger::logError("Failed to create HeartbeatController: " + std::string(e.what()));
            Logger::logInfo("Continuing without Kafka functionality...");
            heartbeatController = nullptr;
        }

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