//
// Created by redeg on 26/04/2025.
//

#include "core/CommandExecutor.hpp"
#include "core/types/Error.hpp"
#include "core/logger/Logger.hpp"
#include <sstream>

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
    types::Result CommandExecutor::sendCommandAndAwaitResponse(const std::string &command, uint16_t commandNumber) {
        serial_->send(command);

        while (true) {
            std::string response = serial_->receiveLine();

            if (response.empty()) {
                continue; // Aspetta fino al prossimo messaggio
            }

            // Solo righe che iniziano per OK, ERR o RESEND sono considerate valide
            if (response.find("OK") == 0 || response.find("ERR") == 0 || response.find("RESEND") == 0) {
                return parseResponse(response, commandNumber);
            }
        }
    }

/**
 * @brief Analizza la risposta ricevuta dalla stampante.
 */
    types::Result CommandExecutor::parseResponse(const std::string &response, uint16_t expectedNumber) {
        std::istringstream iss(response);
        std::string token;
        iss >> token;

        if (token == "OK") {
            int receivedNumber = 0;
            iss >> token; // N<num>
            if (token[0] == 'N') {
                receivedNumber = std::stoi(token.substr(1));
            }
            if (receivedNumber != expectedNumber) {
                std::stringstream ss;
                ss << "ACK number mismatch: Received: " << receivedNumber << " Expected>: " << expectedNumber;
                Logger::logError(ss.str());
                throw types::DriverException("ACK number mismatch");
            }
            return {types::ResultCode::Success, "Command acknowledged."};
        }

        if (token == "ERR") {
            std::string errorMessage;
            std::getline(iss, errorMessage);
            return {types::ResultCode::Error, "ERR: " + errorMessage};
        }

        if (token == "RESEND") {
            int requestedNumber = 0;
            iss >> token;
            if (token[0] == 'N') {
                requestedNumber = std::stoi(token.substr(1));
            }

            std::string resendCommand = context_->getCommandText(requestedNumber);
            if (resendCommand.empty()) {
                Logger::logError("[RESEND] FAILED: EMPTY RESEND REQUEST");
                throw types::ResendFailedException();
            }

            Logger::logWarning("[RESEND] Resending command N" + std::to_string(requestedNumber));

            serial_->send(resendCommand);
            return sendCommandAndAwaitResponse(resendCommand, requestedNumber);
        }


        return {types::ResultCode::Error, "Unknown response."};
    }

} // namespace core
