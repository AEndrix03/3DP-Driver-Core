//
// Created by redeg on 26/04/2025.
//

#include "core/DriverInterface.hpp"
#include "core/CommandBuilder.hpp"
#include <sstream>

namespace core {

/**
 * @brief Costruttore della DriverInterface.
 */
    DriverInterface::DriverInterface(std::shared_ptr<PrinterInterface> printer)
            : printer_(std::move(printer)), commandContext_(std::make_unique<CommandContext>()),
              currentState_(PrintState::Idle) {
    }

/**
 * @brief Esegue un movimento sugli assi X, Y, Z con feedrate specificato.
 */
    types::Result DriverInterface::moveTo(float x, float y, float z, float feedrate) {
        std::vector<std::string> params = {
                "X" + std::to_string(x),
                "Y" + std::to_string(y),
                "Z" + std::to_string(z),
                "F" + std::to_string(feedrate)
        };
        return sendCommandInternal('M', 10, params);
    }

/**
 * @brief Estrude una quantità di filamento a una velocità specifica.
 */
    types::Result DriverInterface::extrude(float millimeters, float feedrate) {
        std::vector<std::string> params = {
                "E" + std::to_string(millimeters),
                "F" + std::to_string(feedrate)
        };
        return sendCommandInternal('A', 10, params);
    }

/**
 * @brief Comanda il rientro a home degli assi.
 */
    types::Result DriverInterface::homeAxes() {
        return sendCommandInternal('S', 0, {});
    }

/**
 * @brief Imposta la temperatura del piano riscaldato.
 */
    types::Result DriverInterface::setBedTemperature(int temperature) {
        std::vector<std::string> params = {
                "T" + std::to_string(temperature)
        };
        return sendCommandInternal('T', 10, params);
    }

/**
 * @brief Imposta la velocità di rotazione della ventola.
 */
    types::Result DriverInterface::fanSetSpeed(int speedPercent) {
        std::vector<std::string> params = {
                "S" + std::to_string(speedPercent)
        };
        return sendCommandInternal('F', 10, params);
    }

/**
 * @brief Arresta immediatamente il sistema.
 */
    types::Result DriverInterface::emergencyStop() {
        return sendCommandInternal('S', 4, {});
    }

/**
 * @brief Invia un comando custom raw.
 */
    types::Result DriverInterface::sendCustomCommand(const std::string &rawCommand) {
        if (!printer_->sendCommand(rawCommand)) {
            return {types::ResultCode::Error, "Failed to send custom command."};
        }
        return {types::ResultCode::Success, "Custom command sent."};
    }

/**
 * @brief Ottiene lo stato attuale del driver.
 */
    PrintState DriverInterface::getState() const {
        return currentState_;
    }

/**
 * @brief Logica interna per costruire e inviare un comando standardizzato.
 */
    types::Result
    DriverInterface::sendCommandInternal(char category, int code, const std::vector<std::string> &params) {
        uint16_t cmdNum = commandContext_->nextCommandNumber();
        std::string command = CommandBuilder::buildCommand(cmdNum, category, code, params);
        commandContext_->storeCommand(cmdNum, command);

        if (!printer_->sendCommand(command)) {
            currentState_ = PrintState::Error;
            return {types::ResultCode::Error, "Failed to send command."};
        }

        currentState_ = PrintState::Running;
        return {types::ResultCode::Success, "Command sent."};
    }

} // namespace core
