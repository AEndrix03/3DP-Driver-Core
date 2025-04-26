#include "core/DriverInterface.hpp"
#include "core/serial/impl/RealSerialPort.hpp"
#include "core/printer/impl/RealPrinter.hpp"
#include "core/printer/PrintState.hpp"
#include "core/types/Result.hpp"
#include "core/printer/Printer.hpp"

#include <iostream>
#include <memory>

using namespace core;

int main() {
    try {
        std::cout << "Starting 3DP Driver Core with RealSerialPort..." << std::endl;

        //std::string portName = "/dev/ttyUSB0"; // Linux
        std::string portName = "COM4"; // Windows

        std::shared_ptr<SerialPort> serialPort = std::make_shared<RealSerialPort>(portName, 115200);
        std::shared_ptr<Printer> printer = std::make_shared<RealPrinter>(serialPort);
        DriverInterface driver(printer, serialPort);

        // Inizializza connessione
        printer->initialize();

        // MOVE TO
        auto moveResult = driver.motion()->moveTo(10.0f, 20.0f, 5.0f, 1500.0f);
        if (moveResult.isSuccess()) {
            std::cout << "[Driver] Move command success!" << std::endl;
        } else {
            std::cerr << "[Driver] Move failed: " << moveResult.message << std::endl;
        }


        // Shutdown (non obbligatorio per ora)
        printer->shutdown();

    } catch (const std::exception &ex) {
        std::cerr << "[Fatal Error]: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
