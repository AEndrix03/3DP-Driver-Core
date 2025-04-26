//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "CommandContext.hpp"
#include "core/printer/PrintState.hpp"
#include "core/printer/Printer.hpp"
#include "types/Result.hpp"
#include "CommandExecutor.hpp"
#include <memory>
#include <vector>

namespace core {

/**
 * @brief Interfaccia principale del driver, esposta verso applicazioni esterne.
 */
    class DriverInterface {
    public:
        /**
         * @brief Costruttore.
         * @param printer Oggetto che implementa Printer.
         */
        explicit DriverInterface(std::shared_ptr<Printer> printer, std::shared_ptr<SerialPort> serialPort);

        /**
         * @brief Movimento sugli assi X, Y, Z con feedrate specificato.
         */
        types::Result moveTo(float x, float y, float z, float feedrate);

        /**
         * @brief Estrusione del filamento.
         */
        types::Result extrude(float millimeters, float feedrate);

        /**
         * @brief Comando di homing assi.
         */
        types::Result homeAxes();

        /**
         * @brief Imposta temperatura del bed.
         */
        types::Result setBedTemperature(int temperature);

        /**
         * @brief Imposta la velocità della ventola.
         */
        types::Result fanSetSpeed(int speedPercent);

        /**
         * @brief Arresto di emergenza.
         */
        types::Result emergencyStop();

        /**
         * @brief Invio di comando custom raw.
         */
        types::Result sendCustomCommand(const std::string &rawCommand);

        /**
         * @brief Recupera lo stato corrente del driver.
         */
        PrintState getState() const;

    private:
        std::shared_ptr<Printer> printer_;
        std::shared_ptr<SerialPort> serialPort_;
        std::shared_ptr<CommandContext> commandContext_;
        std::shared_ptr<CommandExecutor> commandExecutor_;
        PrintState currentState_;

        types::Result sendCommandInternal(char category, int code, const std::vector<std::string> &params);
    };

}
