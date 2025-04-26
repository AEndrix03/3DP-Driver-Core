//
// Created by redeg on 26/04/2025.
//

#include "core/printer/impl/RealPrinter.hpp"
#include "core/serial/SerialPort.hpp"
#include <memory>
#include <iostream>

namespace core {

/**
 * @brief Costruttore di RealPrinter che riceve la SerialPort.
 */
    RealPrinter::RealPrinter(std::shared_ptr<SerialPort> serial)
            : serial_(std::move(serial)) {}

    void RealPrinter::initialize() {
        std::cout << "[Printer] Initialized." << std::endl;
    }

    bool RealPrinter::sendCommand(const std::string &command) {
        if (serial_) {
            serial_->send(command);
            return true;
        }
        return false;
    }

    void RealPrinter::shutdown() {
        std::cout << "[Printer] Shutdown." << std::endl;
    }

} // namespace core

