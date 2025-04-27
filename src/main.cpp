#include "core/DriverInterface.hpp"
#include "core/serial/impl/RealSerialPort.hpp"
#include "core/printer/impl/RealPrinter.hpp"
#include "core/logger/Logger.hpp"
#include "core/printer/Printer.hpp"

#include <iostream>
#include <memory>

using namespace core;

int main() {
    try {
        Logger::init();
        Logger::logInfo("Starting 3DP Driver Core with RealSerialPort...");

        //std::string portName = "/dev/ttyUSB0"; // Linux
        std::string portName = "COM4"; // Windows

        std::shared_ptr<SerialPort> serialPort = std::make_shared<RealSerialPort>(portName, 115200);
        std::shared_ptr<Printer> printer = std::make_shared<RealPrinter>(serialPort);
        DriverInterface driver(printer, serialPort);

        // Inizializza connessione
        printer->initialize();

        driver.system()->startPrint();
        driver.motion()->moveTo(100.0f, 20.0f, 5.0f, 1500.0f);

        while (true)
            driver.motion()->moveTo(1.0f, 0.0f, 0.0f, 1500.0f);


        // Shutdown (non obbligatorio per ora)
        printer->shutdown();

    } catch (const std::exception &ex) {
        std::stringstream ss;
        ss << "[Fatal Error]: " << ex.what();
        Logger::logError(ss.str());
        return 1;
    }

    return 0;
}
