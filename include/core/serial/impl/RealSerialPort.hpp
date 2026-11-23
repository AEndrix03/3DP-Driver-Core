#pragma once

#include "../SerialPort.hpp"
#include <boost/asio.hpp>
#include <string>
#include <memory>
#include <mutex>

#ifdef _WIN32

#include <windows.h>

#else
#include <sys/ioctl.h>
#include <termios.h>
#endif

namespace core {

/**
 * @brief Implementazione di SerialPort usando Boost.Asio
 */
    class RealSerialPort : public SerialPort {
    public:
        RealSerialPort(const std::string &portName, uint32_t baudrate);

        ~RealSerialPort();

        void send(const std::string &data) override;

        void sendRaw(const std::string &bytes) override;

        std::string receiveLine() override;

        bool isOpen() const override;

    private:
        boost::asio::io_context io_context_;
        std::unique_ptr<boost::asio::serial_port> serial_port_;
        std::string buffer_;
        std::mutex writeMutex_;

        /// Estrae dal buffer la prima riga completa non vuota, se presente.
        bool extractLine(std::string &line);

        void configurePort(uint32_t baudrate);

        /**
         * @brief Triggers device reset via DTR signal manipulation (generic serial device)
         */
        void triggerDeviceReset();

        /**
         * @brief Returns number of bytes available to read on the serial port (platform-specific)
         */
        uint32_t getAvailableBytes();

        /**
         * @brief Clears the serial buffer
         */
        void clearBuffer();
    };

} // namespace core

