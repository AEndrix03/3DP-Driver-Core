//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "./../SerialPort.hpp"
#include "serial/serial.h"
#include <string>
#include <memory>

namespace core {

/**
 * @brief Implementazione reale di SerialPort usando la libreria wjwwood/serial.
 */
    class RealSerialPort : public SerialPort {
    public:
        RealSerialPort(const std::string &portName, uint32_t baudrate);

        void send(const std::string &data) override;

        std::string receiveLine() override;

        bool isOpen() const override;

    private:
        std::unique_ptr<serial::Serial> serial_;
    };

} // namespace core
