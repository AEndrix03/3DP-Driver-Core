#include "core/serial/impl/RealSerialPort.hpp"
#include "logger/Logger.hpp"
#include <boost/system/error_code.hpp>
#include <iostream>
#include <thread>
#include <chrono>

namespace core {
    RealSerialPort::RealSerialPort(const std::string &portName, uint32_t baudrate)
            : io_context_(), serial_port_(nullptr) {
        try {
            serial_port_ = std::make_unique<boost::asio::serial_port>(io_context_, portName);

            if (!serial_port_->is_open()) {
                Logger::logError("[SerialPort] Failed to open " + portName);
                serial_port_.reset();
                return;
            }

            // Configure port BEFORE DTR manipulation
            configurePort(baudrate);

            // Force device reset via DTR (generic serial device)
            triggerDeviceReset();

            Logger::logInfo("[SerialPort] Opened successfully on " + portName);

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
        if (!serial_port_ || !serial_port_->is_open()) return;

        boost::system::error_code ec;

        // Set baud rate
        serial_port_->set_option(boost::asio::serial_port_base::baud_rate(baudrate), ec);
        if (ec) {
            Logger::logWarning("[SerialPort] Failed to set baud rate: " + ec.message());
        }

        // Set character size (8 bits) - with better error handling
        try {
            serial_port_->set_option(boost::asio::serial_port_base::character_size(8));
        } catch (const boost::system::system_error &e) {
            Logger::logWarning("[SerialPort] Character size setting failed (non-critical): " + std::string(e.what()));
        }

        // Set parity (none)
        serial_port_->set_option(boost::asio::serial_port_base::parity(
                boost::asio::serial_port_base::parity::none), ec);
        if (ec) {
            Logger::logWarning("[SerialPort] Failed to set parity: " + ec.message());
        }

        // Set stop bits (1)
        serial_port_->set_option(boost::asio::serial_port_base::stop_bits(
                boost::asio::serial_port_base::stop_bits::one), ec);
        if (ec) {
            Logger::logWarning("[SerialPort] Failed to set stop bits: " + ec.message());
        }

        // Set flow control (hardware - important for DTR)
        serial_port_->set_option(boost::asio::serial_port_base::flow_control(
                boost::asio::serial_port_base::flow_control::hardware), ec);
        if (ec) {
            // Try without hardware flow control if it fails
            serial_port_->set_option(boost::asio::serial_port_base::flow_control(
                    boost::asio::serial_port_base::flow_control::none), ec);
            if (ec) {
                Logger::logWarning("[SerialPort] Failed to set flow control: " + ec.message());
            }
        }
    }

    void RealSerialPort::triggerDeviceReset() {
        if (!serial_port_ || !serial_port_->is_open()) return;

        Logger::logInfo("[SerialPort] Triggering device reset via DTR...");

#ifdef _WIN32
        // Windows-specific DTR control
        HANDLE hSerial = serial_port_->native_handle();

        // Set DTR high
        EscapeCommFunction(hSerial, SETDTR);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Set DTR low (triggers reset)
        EscapeCommFunction(hSerial, CLRDTR);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Set DTR high again
        EscapeCommFunction(hSerial, SETDTR);

        // Clear any garbage in buffers
        PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);
#else
        // Linux/Mac DTR control
        int fd = serial_port_->native_handle();
        int status;

        // Get current status
        ioctl(fd, TIOCMGET, &status);

        // Set DTR high
        status |= TIOCM_DTR;
        ioctl(fd, TIOCMSET, &status);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Set DTR low (triggers reset)
        status &= ~TIOCM_DTR;
        ioctl(fd, TIOCMSET, &status);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Set DTR high again
        status |= TIOCM_DTR;
        ioctl(fd, TIOCMSET, &status);

        // Clear buffers
        tcflush(fd, TCIOFLUSH);
#endif

        // Wait for device to boot (typically 2 seconds for bootloader)
        Logger::logInfo("[SerialPort] Waiting for device bootloader...");
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));

        // Clear any bootloader messages
        clearBuffer();
    }

    uint32_t RealSerialPort::getAvailableBytes() {
        if (!serial_port_ || !serial_port_->is_open()) return 0;

#ifdef _WIN32
        HANDLE hSerial = serial_port_->native_handle();
        COMSTAT comStat;
        DWORD errors = 0;
        if (!ClearCommError(hSerial, &errors, &comStat)) {
            return 0;
        }
        return static_cast<uint32_t>(comStat.cbInQue);
#else
        int fd = serial_port_->native_handle();
        int bytes = 0;
        if (ioctl(fd, FIONREAD, &bytes) == -1) {
            return 0;
        }
        return static_cast<uint32_t>(bytes);
#endif
    }

    void RealSerialPort::clearBuffer() {
        if (!serial_port_ || !serial_port_->is_open()) return;

        boost::system::error_code ec;
        char temp[256];

        // Non-blocking read to clear buffer
        while (getAvailableBytes() > 0) {
            serial_port_->read_some(boost::asio::buffer(temp, sizeof(temp)), ec);
            if (ec) break;
        }

        buffer_.clear();
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
            return "";
        }

        boost::system::error_code ec;
        std::string line;

        try {
            static int timeoutCount = 0;
            boost::asio::steady_timer timer(io_context_);
            timer.expires_after(std::chrono::milliseconds(500));

            bool timeout = false;
            timer.async_wait([&](const boost::system::error_code &) {
                timeout = true;
                timeoutCount++;
                serial_port_->cancel();
            });

            boost::asio::read_until(*serial_port_, boost::asio::dynamic_buffer(buffer_), '\n', ec);
            timer.cancel();

            if (timeout) {
                if (timeoutCount % 100 == 0) {
                    Logger::logWarning("[SerialPort] " + std::to_string(timeoutCount) + " timeouts occurred");
                }
                return "";
            }

            timeoutCount = 0;

            if (ec) {
                if (ec != boost::asio::error::operation_aborted) {
                    Logger::logError("[SerialPort] Read error: " + ec.message());

                    if (ec == boost::asio::error::broken_pipe ||
                        ec == boost::asio::error::connection_reset) {
                        Logger::logError("[SerialPort] Connection lost - attempting recovery");
                        return "CONN_LOST";
                    }
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

        } catch (const std::exception &e) {
            Logger::logError("[SerialPort] Exception: " + std::string(e.what()));
            return "SERIAL_ERROR";
        }

        return line;
    }

    bool RealSerialPort::isOpen() const {
        return serial_port_ && serial_port_->is_open();
    }
} // namespace core

