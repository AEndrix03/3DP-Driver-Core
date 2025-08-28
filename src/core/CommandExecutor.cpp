//
// Created by redeg on 26/04/2025.
//

#include "core/CommandExecutor.hpp"
#include "core/types/Error.hpp"
#include "logger/Logger.hpp"
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
        std::lock_guard<std::mutex> lock(serialMutex_);

        serial_->send(command);

        int retries = 0;
        const int maxRetries = 3; // FIX: Ridotto da 5 a 3
        const int baseTimeoutMs = 2000; // FIX: Timeout più aggressivo

        types::Result resultAccumulated;
        resultAccumulated.code = types::ResultCode::Skip;
        resultAccumulated.commandNumber = expectedNumber;

        while (retries <= maxRetries) {
            auto startTime = std::chrono::steady_clock::now();
            auto maxTimeout = std::chrono::milliseconds(baseTimeoutMs + retries * 1000);

            while (true) {
                if (std::chrono::steady_clock::now() - startTime > maxTimeout) {
                    Logger::logWarning("[CommandExecutor] Timeout N" + std::to_string(expectedNumber) +
                                       " (attempt " + std::to_string(retries + 1) + ")");
                    break;
                }

                std::string response = serial_->receiveLine();

                // FIX: Gestione errori di connessione
                if (response == "CONN_LOST" || response == "SERIAL_ERROR") {
                    Logger::logError("[CommandExecutor] Serial connection issue for N" +
                                     std::to_string(expectedNumber));
                    return {types::ResultCode::Error, "Serial connection lost"};
                }

                if (response.empty() || response.find_first_not_of(" \t\r\n") == std::string::npos) {
                    continue;
                }

                // Store non-OK responses for debug
                if (response.find("OK") == std::string::npos &&
                    response.find("BUSY") == std::string::npos) {
                    resultAccumulated.body.push_back(response);
                }

                types::Result result = parseResponse(response, expectedNumber);

                if (result.isSuccess()) {
                    resultAccumulated.code = types::ResultCode::Success;
                    resultAccumulated.message = result.message;
                    return resultAccumulated;
                }

                if (result.isSkip()) {
                    continue;
                }

                if (result.isBusy()) {
                    // FIX: Backoff progressivo per BUSY
                    std::this_thread::sleep_for(std::chrono::milliseconds(50 * (retries + 1)));
                    startTime = std::chrono::steady_clock::now(); // Reset timeout
                    continue;
                }

                if (result.isError()) {
                    Logger::logWarning("[CommandExecutor] Error response: " + result.message);
                    break;
                }
            }

            retries++;
            if (retries <= maxRetries) {
                // FIX: Backoff esponenziale per retry
                std::this_thread::sleep_for(std::chrono::milliseconds(100 * retries));
                Logger::logWarning("[CommandExecutor] Retry " + std::to_string(retries) +
                                   "/" + std::to_string(maxRetries) + " for N" + std::to_string(expectedNumber));
                serial_->send(command);
            }
        }

        // FIX: Non throw, ritorna errore
        Logger::logError("[CommandExecutor] Max retries exceeded for N" + std::to_string(expectedNumber));
        return {types::ResultCode::Error, "Max retries exceeded"};
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

        if (token == "BUSY") {
            return {types::ResultCode::Busy, "Busy Serial: Command is processing."};
        }

        if (token == "RESEND") {
            iss >> token; // N<num>
            if (token[0] == 'N') {
                int requestedNumber = std::stoi(token.substr(1));
                std::string resendCommand = context_->getCommandText(requestedNumber);
                if (resendCommand.empty()) {
                    throw types::ResendFailedException();
                }

                Logger::logWarning("[RESEND] Resending printer-command N" + std::to_string(requestedNumber));
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
