//
// Created by redeg on 04/09/2025.
//

#pragma once

#include <exception>
#include <string>
#include <cstdint>

namespace core::exceptions {
    class ResendErrorCommandException : public std::exception {
    private:
        std::string message;
        uint32_t commandNumber;

    public:
        ResendErrorCommandException(const std::string &msg, const uint32_t commandNumber)
                : message(msg), commandNumber(commandNumber) {}

        const char *what() const noexcept override {
            return message.c_str();
        }

        uint32_t getCommandNumber() const noexcept {
            return commandNumber;
        }
    };
}