//
// Created by redeg on 26/04/2025.
//

#include "core/CommandExecutor.hpp"
#include "core/types/Error.hpp"
#include "core/logger/Logger.hpp"
#include <sstream>
#include <chrono>

namespace core {

/**
 * @brief Costruttore di CommandExecutor.
 */
    CommandExecutor::CommandExecutor(std::shared_ptr<SerialPort> serial, std::shared_ptr<CommandContext> context)
            : serial_(std::move(serial)), context_(std::move(context)) {
    }

/**
 * @brief Invia il comando e aspetta un ACK o un ERR.
 */
    types::Result CommandExecutor::sendCommandAndAwaitResponse(const std::string &command, uint16_t expectedNumber) {
        serial_->send(command);

        int retries = 0;
        const int maxRetries = 3;
        const auto maxTimeoutPerAttempt = std::chrono::seconds(5);

        while (retries <= maxRetries) {
            auto startTime = std::chrono::steady_clock::now();

            while (true) {
                if (std::chrono::steady_clock::now() - startTime > maxTimeoutPerAttempt) {
                    Logger::logWarning(
                            "[Timeout] No valid response within timeout for N" + std::to_string(expectedNumber));
                    break;
                }

                std::string response = serial_->receiveLine();
                if (response.empty() || response.find_first_not_of(" \t\r\n") == std::string::npos) {
                    continue;
                }
                
                types::Result result = parseResponse(response, expectedNumber);

                if (result.isSuccess()) {
                    return result;
                }

                if (result.isSkip()) {
                    continue;
                }

                if (result.isError()) {
                    Logger::logWarning("[Mismatch/Error] " + result.message);
                    break;
                }
            }

            retries++;
            if (retries <= maxRetries) {
                Logger::logWarning("[Retry] Resending command N" + std::to_string(expectedNumber) + " (" +
                                   std::to_string(retries) + "/" + std::to_string(maxRetries) + ")");
                serial_->send(command);
            } else {
                Logger::logError("[Fatal Error] Max retries reached for N" + std::to_string(expectedNumber));
                throw types::DriverException("Max retries reached for N" + std::to_string(expectedNumber));
            }
        }

        throw types::DriverException("Unreachable fatal error in sendCommandAndAwaitResponse");
    }


    /**
 * @brief Analizza la risposta ricevuta dalla stampante.
 */
    types::Result CommandExecutor::parseResponse(const std::string &response, uint16_t expectedNumber) {
        std::istringstream iss(response);
        std::string token;
        iss >> token;

        if (token == "OK" || token == "DUPLICATE") {
            iss >> token; // N<num>
            if (token[0] == 'N') {
                int receivedNumber = std::stoi(token.substr(1));
                if (receivedNumber != expectedNumber) {
                    // 👇 NON throw, ritorna errore
                    return {types::ResultCode::Error,
                            "ACK number mismatch: Received: " + std::to_string(receivedNumber) + " Expected: " +
                            std::to_string(expectedNumber)};
                }
            }
            return {types::ResultCode::Success, "Command acknowledged."};
        }

        if (token == "RESEND") {
            iss >> token; // N<num>
            if (token[0] == 'N') {
                int requestedNumber = std::stoi(token.substr(1));
                std::string resendCommand = context_->getCommandText(requestedNumber);
                if (resendCommand.empty()) {
                    throw types::ResendFailedException();
                }

                Logger::logWarning("[RESEND] Resending command N" + std::to_string(requestedNumber));
                serial_->send(resendCommand);
                return sendCommandAndAwaitResponse(resendCommand, requestedNumber);
            }
        }

        if (token == "ERR") {
            return {types::ResultCode::Error, response};
        }

        return {types::ResultCode::Skip, "Response message. Skipping results..."};
    }

} // namespace core
