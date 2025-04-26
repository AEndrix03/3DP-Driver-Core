//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "./../Printer.hpp"
#include "./../../serial/SerialPort.hpp"
#include <memory>

namespace core {

/**
 * @brief Implementazione concreta di Printer usando SerialPort.
 */
    class RealPrinter : public Printer {
    public:
        explicit RealPrinter(std::shared_ptr<SerialPort> serial);

        void initialize() override;

        bool sendCommand(const std::string &command) override;

        void shutdown() override;

    private:
        std::shared_ptr<SerialPort> serial_;
    };

} // namespace core