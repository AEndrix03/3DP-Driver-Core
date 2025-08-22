#include "core/serial/impl/RealSerialPort.hpp"
#include "logger/Logger.hpp"
#include <boost/system/error_code.hpp>
#include <iostream>

namespace core {
    RealSerialPort::RealSerialPort(const std::string &portName, uint32_t baudrate)
        : io_context_(), serial_port_(nullptr) {
        try {
            serial_port_ = std::make_unique<boost::asio::serial_port>(io_context_, portName);
            configurePort(baudrate);

            if (isOpen()) {
                Logger::logInfo("[SerialPort] Opened successfully on " + portName);
            } else {
                Logger::logError("[SerialPort] Failed to open " + portName);
            }
        } catch (const boost::system::system_error &e) {
            Logger::logError("[SerialPort] Failed to open " + portName + ": " + e.what());
            serial_port_.reset();
        }
    }

    RealSerialPort::~RealSerialPort() {
        if (serial_port_ && serial_port_->is_open()) {
            boost::system::error_code ec;
            serial_port_->close(ec);
            if (ec) {
                Logger::logError("[SerialPort] Error closing port: " + ec.message());
            }
        }
    }

    void RealSerialPort::configurePort(uint32_t baudrate) {
        if (!serial_port_) return;

        boost::system::error_code ec;

        // Set baud rate
        serial_port_->set_option(boost::asio::serial_port_base::baud_rate(baudrate), ec);
        if (ec) {
            Logger::logError("[SerialPort] Failed to set baud rate: " + ec.message());
            return;
        }

        // Set character size (8 bits)
        serial_port_->set_option(boost::asio::serial_port_base::character_size(8), ec);
        if (ec) {
            Logger::logError("[SerialPort] Failed to set character size: " + ec.message());
            return;
        }

        // Set parity (none)
        serial_port_->set_option(boost::asio::serial_port_base::parity(
                                     boost::asio::serial_port_base::parity::none), ec);
        if (ec) {
            Logger::logError("[SerialPort] Failed to set parity: " + ec.message());
            return;
        }

        // Set stop bits (1)
        serial_port_->set_option(boost::asio::serial_port_base::stop_bits(
                                     boost::asio::serial_port_base::stop_bits::one), ec);
        if (ec) {
            Logger::logError("[SerialPort] Failed to set stop bits: " + ec.message());
            return;
        }

        // Set flow control (none)
        serial_port_->set_option(boost::asio::serial_port_base::flow_control(
                                     boost::asio::serial_port_base::flow_control::none), ec);
        if (ec) {
            Logger::logError("[SerialPort] Failed to set flow control: " + ec.message());
            return;
        }
    }

    void RealSerialPort::send(const std::string &data) {
        if (!serial_port_ || !serial_port_->is_open()) {
            Logger::logError("[SerialPort] ERROR: Serial port not open when trying to send!");
            return;
        }

        std::string message = data + "\n";
        boost::system::error_code ec;

        size_t bytes_written = boost::asio::write(*serial_port_,
                                                  boost::asio::buffer(message), ec);

        if (ec) {
            Logger::logError("[SerialPort] Write error: " + ec.message());
            return;
        }

        if (bytes_written != message.length()) {
            Logger::logWarning("[SerialPort] Not all bytes written: " +
                               std::to_string(bytes_written) + "/" + std::to_string(message.length()));
        }

        Logger::logInfo("[TX] " + data);
    }

    std::string RealSerialPort::receiveLine() {
        if (!serial_port_ || !serial_port_->is_open()) {
            Logger::logError("[SerialPort] ERROR: Serial port not open when trying to receive!");
            return "";
        }

        boost::system::error_code ec;
        std::string line;

        try {
            // Read with timeout
            boost::asio::steady_timer timer(io_context_);
            timer.expires_after(std::chrono::seconds(1)); // 1 second timeout

            bool timeout = false;
            timer.async_wait([&](const boost::system::error_code &) {
                timeout = true;
                serial_port_->cancel();
            });

            boost::asio::read_until(*serial_port_, boost::asio::dynamic_buffer(buffer_), '\n', ec);
            timer.cancel();

            if (timeout) {
                return ""; // Timeout, return empty (not an error for polling)
            }

            if (ec) {
                if (ec == boost::asio::error::operation_aborted) {
                    return ""; // Normal timeout
                }
                Logger::logError("[SerialPort] Read error: " + ec.message());

                // Check if port is still open after error
                if (!serial_port_->is_open()) {
                    Logger::logError("[SerialPort] Serial port closed unexpectedly!");
                }
                return "";
            }

            // Extract line from buffer
            auto pos = buffer_.find('\n');
            if (pos != std::string::npos) {
                line = buffer_.substr(0, pos);
                buffer_.erase(0, pos + 1);

                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                if (!line.empty()) {
                    Logger::logInfo("[RX] Received: " + line);
                }
            }
        } catch (const boost::system::system_error &e) {
            Logger::logError("[SerialPort] Exception while reading: " + std::string(e.what()));

            // Try to detect if device was unplugged
            if (!isOpen()) {
                Logger::logError("[SerialPort] Device appears to be disconnected!");
            }
            return "";
        }

        return line;
    }

    bool RealSerialPort::isOpen() const {
        return serial_port_ && serial_port_->is_open();
    }
} // namespace core
