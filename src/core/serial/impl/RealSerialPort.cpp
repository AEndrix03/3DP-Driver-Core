#include "core/serial/impl/RealSerialPort.hpp"
#include <iostream>

namespace core {

    RealSerialPort::RealSerialPort(const std::string &portName, uint32_t baudrate)
            : serial_(std::make_unique<serial::Serial>(portName, baudrate, serial::Timeout::simpleTimeout(1000))) {
        if (serial_ && serial_->isOpen()) {
            std::cout << "[SerialPort] Opened successfully on " << portName << std::endl;
        } else {
            std::cerr << "[SerialPort] Failed to open " << portName << std::endl;
        }
    }

    void RealSerialPort::send(const std::string &data) {
        if (serial_ && serial_->isOpen()) {
            std::cout << "[TX] " << data << std::endl;
            serial_->write(data + "\n");
        } else {
            std::cerr << "[Serial] ERROR: Serial port not open when trying to send!" << std::endl;
        }
    }

    std::string RealSerialPort::receiveLine() {
        if (serial_ && serial_->isOpen()) {
            std::cout << "[RX] Waiting for data..." << std::endl;
            try {
                auto received = serial_->readline(256, "\n");
                std::cout << "[RX] Received: " << received << std::endl;
                return received;
            } catch (const std::exception &ex) {
                std::cerr << "[Serial ERROR] Exception while reading: " << ex.what() << std::endl;
                return "";
            }
        }
        std::cerr << "[Serial] ERROR: Serial port not open when trying to receive!" << std::endl;
        return "";
    }

    bool RealSerialPort::isOpen() const {
        return serial_ && serial_->isOpen();
    }
    
} // namespace core
