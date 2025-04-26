#include "core/serial/impl/RealSerialPort.hpp"
#include "core/logger/Logger.hpp"
#include <iostream>

namespace core {

    RealSerialPort::RealSerialPort(const std::string &portName, uint32_t baudrate)
            : serial_(std::make_unique<serial::Serial>(portName, baudrate, serial::Timeout::simpleTimeout(1000))) {
        if (serial_ && serial_->isOpen()) {
            Logger::logInfo("[SerialPort] Opened successfully on " + portName);
        } else {
            Logger::logError("[SerialPort] Failed to open " + portName);
        }
    }

    void RealSerialPort::send(const std::string &data) {
        if (serial_ && serial_->isOpen()) {
            Logger::logInfo("[TX] " + data);
            serial_->write(data + "\n");
        } else {
            Logger::logError("[Serial] ERROR: Serial port not open when trying to send!");
        }
    }

    std::string RealSerialPort::receiveLine() {
        if (serial_ && serial_->isOpen()) {
            try {
                auto received = serial_->readline(256, "\n");
                if (!received.empty()) {
                    Logger::logInfo("[RX] Received: " + received);
                }
                return received;
            } catch (const std::exception &ex) {
                std::stringstream ss;
                ss << "[Serial ERROR] Exception while reading: " << ex.what();
                Logger::logError(ss.str());
                return "";
            }
        }
        Logger::logError("[Serial] ERROR: Serial port not open when trying to receive!");
        return "";
    }

    bool RealSerialPort::isOpen() const {
        return serial_ && serial_->isOpen();
    }

} // namespace core
