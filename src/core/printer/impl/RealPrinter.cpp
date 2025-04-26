//
// Created by redeg on 26/04/2025.
//

#include "core/printer/impl/RealPrinter.hpp"
#include "core/serial/SerialPort.hpp"
#include <iostream>

namespace core {

    RealPrinter::RealPrinter(std::shared_ptr<SerialPort> serial)
            : serial_(std::move(serial)) {}

    void RealPrinter::initialize() {
        std::cout << "[Printer] Waiting for system ready..." << std::endl;
        if (!serial_ || !serial_->isOpen()) {
            throw std::runtime_error("Serial port not open during printer initialization");
        }

        while (true) {
            std::string line = serial_->receiveLine();
            if (line.empty()) {
                continue; // Timeout, ma retry
            }
            std::cout << "[Printer] RX during init: " << line << std::endl;
            if (line.find("Sistema pronto.") != std::string::npos) {
                std::cout << "[Printer] System is ready!" << std::endl;
                break;
            }
        }
    }

    bool RealPrinter::sendCommand(const std::string &command) {
        if (serial_ && serial_->isOpen()) {
            serial_->send(command);
            return true;
        }
        return false;
    }

    void RealPrinter::shutdown() {
        std::cout << "[Printer] Shutdown requested." << std::endl;
    }

} // namespace core
