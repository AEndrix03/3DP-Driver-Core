#pragma once

#include "../SerialPort.hpp"
#include <boost/asio.hpp>
#include <string>
#include <memory>

namespace core {

/**
 * @brief Implementazione di SerialPort usando Boost.Asio
 */
    class RealSerialPort : public SerialPort {
    public:
        RealSerialPort(const std::string &portName, uint32_t baudrate);

        ~RealSerialPort();

        void send(const std::string &data) override;

        std::string receiveLine() override;

        bool isOpen() const override;

    private:
        boost::asio::io_context io_context_;
        std::unique_ptr<boost::asio::serial_port> serial_port_;
        std::string buffer_;

        void configurePort(uint32_t baudrate);
    };

} // namespace core