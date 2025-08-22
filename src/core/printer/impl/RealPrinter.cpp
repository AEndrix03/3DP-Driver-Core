//
// Created by redeg on 26/04/2025.
//

#include "core/printer/impl/RealPrinter.hpp"
#include "logger/Logger.hpp"

namespace core {
    RealPrinter::RealPrinter(std::shared_ptr<SerialPort> serial)
        : serial_(std::move(serial)) {
    }

    void RealPrinter::initialize() {
        Logger::logInfo("[Printer] Waiting for system ready...");
        if (!serial_ || !serial_->isOpen()) {
            Logger::logError("Serial port not open during printer initialization");
            throw std::runtime_error("Serial port not open during printer initialization");
        }

        while (true) {
            std::string line = serial_->receiveLine();
            if (line.empty()) continue;

            Logger::logInfo("[Printer] RX during init: " + line);
            handleSystemMessage(line);

            if (systemReady_) {
                Logger::logInfo("[Printer] System is ready!");
                break;
            }
        }
    }

    bool RealPrinter::sendCommand(const std::string &command) {
        std::lock_guard<std::mutex> lock(stateMutex_);

        if (!systemReady_) {
            Logger::logWarning("[Printer] System not ready, command rejected: " + command);
            return false;
        }

        if (serial_ && serial_->isOpen()) {
            serial_->send(command);
            return true;
        }
        return false;
    }

    void RealPrinter::shutdown() {
        Logger::logInfo("[Printer] Shutdown requested");
        std::lock_guard<std::mutex> lock(stateMutex_);
        systemReady_ = false;
    }

    bool RealPrinter::isSystemReady() const {
        return systemReady_;
    }

    void RealPrinter::checkSystemStatus() {
        if (!serial_ || !serial_->isOpen()) return;

        std::string line = serial_->receiveLine();
        if (!line.empty()) {
            handleSystemMessage(line);
        }
    }

    void RealPrinter::handleSystemMessage(const std::string &line) {
        if (line.find("Avvio firmware 3DP...") != std::string::npos) {
            Logger::logWarning("[Printer] Arduino reset detected! System restarting...");
            systemReady_ = false;
        } else if (line.find("Sistema pronto.") != std::string::npos) {
            Logger::logInfo("[Printer] System ready");
            systemReady_ = true;
        }
    }
} // namespace core
