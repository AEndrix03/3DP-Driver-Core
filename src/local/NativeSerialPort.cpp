#include "local/NativeSerialPort.hpp"
#include "core/serial/LinuxBaud.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace local {

#ifdef _WIN32

    NativeSerialPort::NativeSerialPort(const std::string &device, unsigned baud) {
        const std::string path = "\\\\.\\" + device;
        HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            error_ = "cannot open " + device + " (error " + std::to_string(GetLastError()) + ")";
            return;
        }

        DCB dcb{};
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(h, &dcb)) {
            error_ = "cannot read port settings";
            CloseHandle(h);
            return;
        }
        dcb.BaudRate = baud;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = TWOSTOPBITS; // 8N2, come il firmware
        dcb.fBinary = TRUE;
        dcb.fParity = FALSE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE; // riavvia la scheda all'apertura
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        if (!SetCommState(h, &dcb)) {
            error_ = "cannot configure " + device + " (error " + std::to_string(GetLastError()) + ")";
            CloseHandle(h);
            return;
        }

        // ReadFile ritorna appena c'e' almeno un byte, altrimenti dopo 50 ms
        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
        timeouts.ReadTotalTimeoutConstant = 50;
        timeouts.WriteTotalTimeoutConstant = 2000;
        SetCommTimeouts(h, &timeouts);
        PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
        handle_ = h;
    }

    void NativeSerialPort::closePort() {
        if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }

    bool NativeSerialPort::isOpen() const { return handle_ != nullptr; }

    void NativeSerialPort::writeAll(const char *data, size_t size) {
        std::lock_guard<std::mutex> lock(writeMutex_);
        if (!handle_) return;
        size_t done = 0;
        while (done < size) {
            DWORD written = 0;
            if (!WriteFile(static_cast<HANDLE>(handle_), data + done, static_cast<DWORD>(size - done), &written,
                           nullptr) || written == 0) {
                error_ = "write failed";
                return;
            }
            done += written;
        }
    }

    int NativeSerialPort::readSome(char *buffer, int size) {
        if (!handle_) return -1;
        DWORD got = 0;
        if (!ReadFile(static_cast<HANDLE>(handle_), buffer, static_cast<DWORD>(size), &got, nullptr)) return -1;
        return static_cast<int>(got);
    }

#else

    NativeSerialPort::NativeSerialPort(const std::string &device, unsigned baud) {
        fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
        if (fd_ < 0) {
            error_ = "cannot open " + device + ": " + std::strerror(errno);
            return;
        }

        termios tty{};
        if (tcgetattr(fd_, &tty) != 0) {
            error_ = std::string("cannot read port settings: ") + std::strerror(errno);
            closePort();
            return;
        }
        cfmakeraw(&tty);
        cfsetispeed(&tty, B115200); // provvisorio: il baud vero viene impostato sotto
        cfsetospeed(&tty, B115200);
        tty.c_cflag |= (CLOCAL | CREAD | HUPCL | CSTOPB); // HUPCL: DTR basso alla chiusura; CSTOPB: 2 bit di stop
        tty.c_cflag &= ~(CRTSCTS | PARENB);
        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 0;
        if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
            error_ = std::string("cannot configure port: ") + std::strerror(errno);
            closePort();
            return;
        }
        // 250000 non e' tra le costanti di termios: va chiesto come numero
        if (!core::setLinuxBaud(fd_, baud)) {
            error_ = "cannot set " + std::to_string(baud) + " baud: " + std::strerror(errno);
            closePort();
            return;
        }
        tcflush(fd_, TCIOFLUSH);
    }

    void NativeSerialPort::closePort() {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
    }

    bool NativeSerialPort::isOpen() const { return fd_ >= 0; }

    void NativeSerialPort::writeAll(const char *data, size_t size) {
        std::lock_guard<std::mutex> lock(writeMutex_);
        size_t done = 0;
        while (fd_ >= 0 && done < size) {
            const ssize_t written = ::write(fd_, data + done, size - done);
            if (written < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;
                error_ = std::string("write failed: ") + std::strerror(errno);
                return;
            }
            done += static_cast<size_t>(written);
        }
    }

    int NativeSerialPort::readSome(char *buffer, int size) {
        if (fd_ < 0) return -1;
        pollfd p{fd_, POLLIN, 0};
        const int ready = ::poll(&p, 1, 50);
        if (ready < 0) return errno == EINTR ? 0 : -1;
        if (ready == 0) return 0;
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) return -1;
        const ssize_t got = ::read(fd_, buffer, static_cast<size_t>(size));
        if (got < 0) return (errno == EINTR || errno == EAGAIN) ? 0 : -1;
        return static_cast<int>(got);
    }

#endif

    NativeSerialPort::~NativeSerialPort() { closePort(); }

    void NativeSerialPort::send(const std::string &data) {
        const std::string line = data + "\n";
        writeAll(line.data(), line.size());
    }

    void NativeSerialPort::sendRaw(const std::string &bytes) { writeAll(bytes.data(), bytes.size()); }

    std::string NativeSerialPort::receiveLine() {
        for (int pass = 0; pass < 2; ++pass) {
            size_t end;
            while ((end = pending_.find('\n')) != std::string::npos) {
                std::string line = pending_.substr(0, end);
                pending_.erase(0, end + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) return line;
            }
            if (pass == 1) break;

            char buffer[256];
            const int got = readSome(buffer, sizeof(buffer));
            if (got < 0) {
                // Porta sparita (cavo staccato): il driver la tratta come collegamento perso
                closePort();
                return "CONN_LOST";
            }
            if (got == 0) break;
            pending_.append(buffer, static_cast<size_t>(got));
        }
        return "";
    }
} // namespace local
