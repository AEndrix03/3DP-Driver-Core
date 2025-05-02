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

#include <iostream>
#include <memory>
#include <vector>

using namespace core;
using namespace translator::gcode;

int main() {
    try {
        Logger::init();
        Logger::logInfo("Starting 3DP Translator test...");

        std::string portName = "COM4"; // o "/dev/ttyUSB0" per Linux
        auto serialPort = std::make_shared<RealSerialPort>(portName, 115200);
        auto printer = std::make_shared<RealPrinter>(serialPort);
        DriverInterface driver(printer, serialPort);

        printer->initialize();

        GCodeTranslator translator(std::make_shared<DriverInterface>(driver));

        translator.registerDispatcher(std::make_unique<MotionDispatcher>(translator.getDriver()));
        translator.registerDispatcher(std::make_unique<SystemDispatcher>(translator.getDriver()));
        translator.registerDispatcher(std::make_unique<ExtruderDispatcher>(translator.getDriver()));
        translator.registerDispatcher(std::make_unique<FanDispatcher>(translator.getDriver()));
        translator.registerDispatcher(std::make_unique<EndstopDispatcher>(translator.getDriver()));
        translator.registerDispatcher(std::make_unique<TemperatureDispatcher>(translator.getDriver()));
        translator.registerDispatcher(std::make_unique<HistoryDispatcher>(translator.getDriver()));

        std::vector<std::string> testCommands = {
                //"G28",
                /*"G1 X10 Y10 Z5 F1500",
                "G10 L3 F400",
                "G11 L5 F400",
                "M106 S255",
                "M107",
                "M104 S200",
                "M140 S60",
                "M119",
                "M701",
                "M702",
                "G999"*/
                "G2 X50 Y0 I25 J25 F1200"
        };

        translator.parseLines(testCommands);

        printer->shutdown();

    } catch (const std::exception &ex) {
        std::stringstream ss;
        ss << "[Fatal Error]: " << ex.what();
        Logger::logError(ss.str());
        return 1;
    }

    return 0;
}
