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

#include "connector/Connector.hpp"
#include "connector/utils/Config.hpp"

#include <iostream>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <csignal>
#include <chrono>

std::atomic<bool> running{true};

void handleSignal(int) {
    running = false;
}

int main() {
    try {
        Logger::init();

        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);

        Logger::logInfo("Starting 3DP Driver and Translator...");

        std::string portName = "COM4";
        auto serialPort = std::make_shared<core::RealSerialPort>(portName, 115200);
        auto printer = std::make_shared<core::RealPrinter>(serialPort);
        core::DriverInterface driver(printer, serialPort);

        printer->initialize();

        translator::gcode::GCodeTranslator translator(std::make_shared<core::DriverInterface>(driver));

        translator.registerDispatcher(std::make_unique<translator::gcode::MotionDispatcher>(translator.getDriver()));
        translator.registerDispatcher(std::make_unique<translator::gcode::SystemDispatcher>(translator.getDriver()));
        translator.registerDispatcher(std::make_unique<translator::gcode::ExtruderDispatcher>(translator.getDriver()));
        translator.registerDispatcher(std::make_unique<translator::gcode::FanDispatcher>(translator.getDriver()));
        translator.registerDispatcher(std::make_unique<translator::gcode::EndstopDispatcher>(translator.getDriver()));
        translator.registerDispatcher(
                std::make_unique<translator::gcode::TemperatureDispatcher>(translator.getDriver()));
        translator.registerDispatcher(std::make_unique<translator::gcode::HistoryDispatcher>(translator.getDriver()));

        auto connector = connector::createConnector();
        std::thread connectorThread([&]() {
            connector->start();
            while (running) std::this_thread::sleep_for(std::chrono::milliseconds(500));
            connector->stop();
        });

        std::vector<std::string> testCommands = {
                "G2 X50 Y0 I25 J25 F1200"
        };
        translator.parseLines(testCommands);

        while (running) std::this_thread::sleep_for(std::chrono::milliseconds(500));

        printer->shutdown();
        if (connectorThread.joinable()) connectorThread.join();

    } catch (const std::exception &ex) {
        Logger::logError(std::string("[Fatal Error]: ") + ex.what());
        return 1;
    }

    return 0;
}
