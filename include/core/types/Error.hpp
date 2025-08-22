#pragma once
#include <stdexcept>
#include <string>

namespace core::types {

class DriverException : public std::runtime_error {
public:
    explicit DriverException(const std::string& msg)
        : std::runtime_error(msg) {}
};

class TimeoutException : public DriverException {
public:
    TimeoutException() : DriverException("Timeout waiting for response") {}
};

class ChecksumMismatchException : public DriverException {
public:
    ChecksumMismatchException() : DriverException("Checksum mismatch detected") {}
};

class ResendFailedException : public DriverException {
public:
    ResendFailedException() : DriverException("Failed to resend printer-command correctly") {}
};

}