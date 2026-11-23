#pragma once

#include "core/serial/SerialPort.hpp"
#include <mutex>
#include <string>

namespace local {

/**
 * @brief Porta seriale senza dipendenze esterne: API Win32 su Windows, termios su Linux.
 *
 * Usata dal servizio locale (3dp), che cosi' si compila con il solo compilatore C++.
 * L'apertura attiva DTR: le schede Arduino si riavviano e ripartono dal banner di avvio.
 */
    class NativeSerialPort : public core::SerialPort {
    public:
        NativeSerialPort(const std::string &device, unsigned baud);

        ~NativeSerialPort() override;

        void send(const std::string &data) override;

        void sendRaw(const std::string &bytes) override;

        /// Una riga (senza fine riga), oppure "" se non ne arriva una entro ~50 ms.
        std::string receiveLine() override;

        bool isOpen() const override;

        const std::string &lastError() const { return error_; }

    private:
        void writeAll(const char *data, size_t size);

        /// Legge i byte disponibili (attesa massima ~50 ms). Ritorna i byte letti, -1 su errore.
        int readSome(char *buffer, int size);

        void closePort();

#ifdef _WIN32
        void *handle_ = nullptr;
#else
        int fd_ = -1;
#endif
        std::mutex writeMutex_;
        std::string pending_;
        std::string error_;
    };
} // namespace local
