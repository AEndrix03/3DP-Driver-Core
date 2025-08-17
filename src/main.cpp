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

// New Event System
#include "events/interfaces/EventBus.hpp"
#include "events/interfaces/CommandHandler.hpp"
#include "events/interfaces/EventEmitter.hpp"
#include "events/utils/EventUtils.hpp"
#include "events/utils/CommandParser.hpp"
#include "events/utils/ResponseBuilder.hpp"

#include <iostream>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <csignal>
#include <chrono>

std::atomic<bool> running{true};

void handleSignal(int) {
    std::cout << "\n[Main] Received shutdown signal..." << std::endl;
    running = false;
}

// ===== BASIC COMMAND HANDLERS (Stubs for Phase 2) =====

/**
 * @brief Basic Heartbeat Handler
 */
class BasicHeartbeatHandler : public events::CommandHandler {
public:
    explicit BasicHeartbeatHandler(std::shared_ptr<core::DriverInterface> driver)
            : driver_(driver) {}

    bool canHandle(const events::Command& command) const override {
        return command.type == "heartbeat" || command.type == "ping";
    }

    void handle(const events::Command& command, events::EventBus& eventBus) override {
        Logger::logInfo("[HeartbeatHandler] Processing: " + command.id);

        std::string statusCode = "IDLE"; // TODO: Get from driver
        if (driver_) {
            switch (driver_->getState()) {
                case core::PrintState::Running: statusCode = "PRINTING"; break;
                case core::PrintState::Paused: statusCode = "PAUSED"; break;
                case core::PrintState::Error: statusCode = "ERROR"; break;
                case core::PrintState::Completed: statusCode = "COMPLETED"; break;
                default: statusCode = "IDLE"; break;
            }
        }

        auto response = events::utils::ResponseBuilder::buildHeartbeatResponse(
                command, command.driverId, statusCode);

        eventBus.emitEvent(response);
        Logger::logInfo("[HeartbeatHandler] Response emitted for: " + command.id);
    }

    std::string getHandlerName() const override { return "BasicHeartbeatHandler"; }
    int getPriority() const override { return 100; }

private:
    std::shared_ptr<core::DriverInterface> driver_;
};

/**
 * @brief Basic Command Handler for GCode execution
 */
class BasicCommandHandler : public events::CommandHandler {
public:
    explicit BasicCommandHandler(std::shared_ptr<translator::gcode::GCodeTranslator> translator)
            : translator_(translator) {}

    bool canHandle(const events::Command& command) const override {
        return command.type == "command" || command.type == "gcode";
    }

    void handle(const events::Command& command, events::EventBus& eventBus) override {
        Logger::logInfo("[CommandHandler] Processing: " + command.id);

        bool success = false;
        std::string info = "";
        std::string exception = "";

        try {
            // Extract GCode command from payload
            if (command.payload.contains("command")) {
                std::string gcodeCommand = command.payload["command"].get<std::string>();
                Logger::logInfo("[CommandHandler] Executing GCode: " + gcodeCommand);

                if (translator_) {
                    translator_->parseLine(gcodeCommand);
                    success = true;
                    info = "GCode executed successfully: " + gcodeCommand;
                } else {
                    exception = "Translator not available";
                }
            } else {
                exception = "No 'command' field in payload";
            }

        } catch (const std::exception& e) {
            exception = std::string(e.what());
        }

        auto response = events::utils::ResponseBuilder::buildCommandResponse(
                command, command.driverId, success, info, exception);

        eventBus.emitEvent(response);
        Logger::logInfo("[CommandHandler] Response emitted for: " + command.id +
                        " (success: " + std::string(success ? "true" : "false") + ")");
    }

    std::string getHandlerName() const override { return "BasicCommandHandler"; }
    int getPriority() const override { return 50; }

private:
    std::shared_ptr<translator::gcode::GCodeTranslator> translator_;
};

/**
 * @brief Logging Event Emitter (stub for Phase 2)
 */
class LoggingEventEmitter : public events::EventEmitter {
public:
    bool emit(const events::Event& event) override {
        Logger::logInfo("[EventEmitter] Event: " + event.type + " (ID: " + event.id + ")");

        if (event.requestId.has_value()) {
            Logger::logInfo("[EventEmitter] Request ID: " + event.requestId.value());
        }

        Logger::logInfo("[EventEmitter] Target: " + event.targetTopic + " (key: " + event.targetKey + ")");

        if (!event.payload.is_null() && !event.payload.empty()) {
            Logger::logInfo("[EventEmitter] Payload: " + event.payload.dump(2));
        }

        ++emittedCount_;
        return true;
    }

    bool isReady() const override { return true; }
    std::string getEmitterName() const override { return "LoggingEventEmitter"; }

    size_t getEmittedCount() const { return emittedCount_; }

private:
    std::atomic<size_t> emittedCount_{0};
};

// ===== MAIN APPLICATION =====

int main() {
    try {
        Logger::init();
        Logger::logInfo("=== 3DP Driver Core - Phase 2: Integrated Event System ===");

        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);

        // ===== Initialize Core Systems =====

        Logger::logInfo("Initializing core systems...");

        std::string portName = std::getenv("SERIAL_PORT") ?
                               std::string(std::getenv("SERIAL_PORT")) : "COM4";
        int baudRate = std::getenv("SERIAL_BAUDRATE") ?
                       std::stoi(std::getenv("SERIAL_BAUDRATE")) : 115200;

        auto serialPort = std::make_shared<core::RealSerialPort>(portName, baudRate);
        auto printer = std::make_shared<core::RealPrinter>(serialPort);
        auto driver = std::make_shared<core::DriverInterface>(printer, serialPort);

        Logger::logInfo("Initializing printer...");
        printer->initialize();

        Logger::logInfo("Setting up GCode translator...");
        auto translator = std::make_shared<translator::gcode::GCodeTranslator>(driver);

        translator->registerDispatcher(std::make_unique<translator::gcode::MotionDispatcher>(driver));
        translator->registerDispatcher(std::make_unique<translator::gcode::SystemDispatcher>(driver));
        translator->registerDispatcher(std::make_unique<translator::gcode::ExtruderDispatcher>(driver));
        translator->registerDispatcher(std::make_unique<translator::gcode::FanDispatcher>(driver));
        translator->registerDispatcher(std::make_unique<translator::gcode::EndstopDispatcher>(driver));
        translator->registerDispatcher(std::make_unique<translator::gcode::TemperatureDispatcher>(driver));
        translator->registerDispatcher(std::make_unique<translator::gcode::HistoryDispatcher>(driver));

        // ===== Initialize Event System =====

        Logger::logInfo("Initializing event system...");

        auto eventBus = events::createEventBus();

        // Register command handlers
        auto heartbeatHandler = std::make_shared<BasicHeartbeatHandler>(driver);
        auto commandHandler = std::make_shared<BasicCommandHandler>(translator);

        eventBus->registerCommandHandler(heartbeatHandler);
        eventBus->registerCommandHandler(commandHandler);

        // Register event emitter
        auto eventEmitter = std::make_shared<LoggingEventEmitter>();
        eventBus->registerEventEmitter(eventEmitter);

        // Wait for system to be ready
        Logger::logInfo("Waiting for event system to be ready...");
        while (!eventBus->isReady()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        Logger::logInfo("Event system ready!");

        auto handlers = eventBus->getRegisteredHandlers();
        auto emitters = eventBus->getRegisteredEmitters();

        Logger::logInfo("Registered " + std::to_string(handlers.size()) + " handlers:");
        for (const auto& name : handlers) {
            Logger::logInfo("  - " + name);
        }

        Logger::logInfo("Registered " + std::to_string(emitters.size()) + " emitters:");
        for (const auto& name : emitters) {
            Logger::logInfo("  - " + name);
        }

        // ===== System Startup Event =====

        auto systemInfo = events::utils::EventUtils::getSystemInfo();
        auto startupEvent = events::utils::EventUtils::createStatusEvent(
                systemInfo.driverId,
                "system_startup",
                nlohmann::json{
                        {"version", systemInfo.version},
                        {"location", systemInfo.location},
                        {"serial_port", systemInfo.serialPort},
                        {"serial_baudrate", systemInfo.baudrate},
                        {"capabilities", systemInfo.capabilities},
                        {"handlers_count", handlers.size()},
                        {"emitters_count", emitters.size()}
                }
        );

        eventBus->emitEvent(startupEvent);

        // ===== Test Event System =====

        Logger::logInfo("Testing event system with sample commands...");

        // Test 1: Heartbeat
        nlohmann::json heartbeatJson = {
                {"id", "test_heartbeat_001"},
                {"type", "heartbeat"},
                {"driverId", systemInfo.driverId},
                {"payload", nlohmann::json::object()}
        };

        auto heartbeatCmd = events::utils::CommandParser::parseFromJson(heartbeatJson);
        eventBus->processCommand(heartbeatCmd);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Test 2: GCode Command
        nlohmann::json gcodeJson = {
                {"id", "test_gcode_001"},
                {"type", "command"},
                {"driverId", systemInfo.driverId},
                {"payload", {{"command", "G28"}}  // Home all axes
                };

                auto gcodeCmd = events::utils::CommandParser::parseFromJson(gcodeJson);
                eventBus->processCommand(gcodeCmd);

                std::this_thread::sleep_for(std::chrono::milliseconds(500));

                Logger::logInfo("=== System fully operational - Event-driven architecture active ===");
                Logger::logInfo("Events emitted: " + std::to_string(eventEmitter->getEmittedCount()));

                // ===== Main Event Loop =====

                while (running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

                    // Periodic health check
                    static auto lastHealthCheck = std::chrono::steady_clock::now();
                    auto now = std::chrono::steady_clock::now();

                    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastHealthCheck).count() >= 30) {
                        auto healthEvent = events::utils::EventUtils::createStatusEvent(
                                systemInfo.driverId,
                                "system_health",
                                nlohmann::json{
                                        {"status", "operational"},
                                        {"serial_connected", serialPort->isOpen()},
                                        {"printer_state", static_cast<int>(driver->getState())},
                                        {"events_emitted", eventEmitter->getEmittedCount()},
                                        {"event_bus_ready", eventBus->isReady()},
                                        {"uptime_seconds", std::chrono::duration_cast<std::chrono::seconds>(
                                                now - lastHealthCheck).count()}
                                }
                        );

                        eventBus->emitEvent(healthEvent);
                        lastHealthCheck = now;
                    }
                }

                // ===== Graceful Shutdown =====

                Logger::logInfo("Initiating graceful shutdown...");

                auto shutdownEvent = events::utils::EventUtils::createStatusEvent(
                systemInfo.driverId,
                "system_shutdown",
                nlohmann::json{
                    {"reason", "user_signal"},
                    {"total_events_emitted", eventEmitter->getEmittedCount()},
                    {"uptime_seconds", std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::steady_clock::now() - std::chrono::steady_clock::now()).count()}
                }
                );

                eventBus->emitEvent(shutdownEvent);

                // Give time for final events to be processed
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));

                printer->shutdown();

                Logger::logInfo("=== Phase 2 Integration completed successfully ===");

        } catch (const std::exception &ex) {
            Logger::logError("[Fatal Error]: " + std::string(ex.what()));
            std::cerr << "[Fatal Error]: " << ex.what() << std::endl;
            return 1;
        }

        return 0;
    }