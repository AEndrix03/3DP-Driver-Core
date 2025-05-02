//
// Created by redeg on 26/04/2025.
//

#include "core/printer/impl/RealPrinter.hpp"
#include "core/serial/SerialPort.hpp"
#include "logger/Logger.hpp"
#include <iostream>

namespace core {

    RealPrinter::RealPrinter(std::shared_ptr<SerialPort> serial)
            : serial_(std::move(serial)) {}

    void RealPrinter::initialize() {
        Logger::logInfo("[Printer] Waiting for system ready...");
        if (!serial_ || !serial_->isOpen()) {
            Logger::logError("Serial port not open during printer initialization");
            throw std::runtime_error("Serial port not open during printer initialization");
        }

        while (true) {
            std::string line = serial_->receiveLine();
            if (line.empty()) {
                continue;
            }
            Logger::logInfo("[Printer] RX during init:" + line);
            if (line.find("Sistema pronto.") != std::string::npos) {
                Logger::logInfo("[Printer] System is ready!");
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
        Logger::logInfo("[Printer] Shutdown requested.");
    }

} // namespace core
