//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "../Printer.hpp"
#include "../../serial/SerialPort.hpp"
#include <memory>
#include <atomic>
#include <mutex>

namespace core {
    class RealPrinter : public Printer {
    public:
        explicit RealPrinter(std::shared_ptr<SerialPort> serial);

        void initialize() override;

        bool sendCommand(const std::string &command) override;

        void shutdown() override;

        // Enhanced methods
        bool isSystemReady() const;

        void checkSystemStatus();

    private:
        std::shared_ptr<SerialPort> serial_;
        std::atomic<bool> systemReady_{false};
        mutable std::mutex stateMutex_;

        void handleSystemMessage(const std::string &line);
    };
} // namespace core
