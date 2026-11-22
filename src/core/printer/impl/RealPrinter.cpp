//
// Created by redeg on 26/04/2025.
//

#include "core/printer/impl/RealPrinter.hpp"
#include "logger/Logger.hpp"
#include <chrono>
#include <stdexcept>

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

        // Il firmware ripete "Sistema pronto." ogni 5 s finche' non riceve un comando valido
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);

        while (true) {
            if (std::chrono::steady_clock::now() > deadline) {
                throw std::runtime_error("Firmware did not report ready within 60 s");
            }
            if (!serial_->isOpen()) {
                throw std::runtime_error("Serial port closed during printer initialization");
            }

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
        Logger::logInfo(line);
        if (line.find("Avvio firmware 3DP...") != std::string::npos) {
            Logger::logWarning("[Printer] Arduino reset detected! System restarting...");
            systemReady_ = false;
        } else if (line.find("Sistema pronto.") != std::string::npos) {
            Logger::logInfo("[Printer] System ready");
            systemReady_ = true;
        }
    }
} // namespace core
