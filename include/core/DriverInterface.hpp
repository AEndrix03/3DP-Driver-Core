//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "CommandContext.hpp"
#include "core/printer/PrintState.hpp"
#include "core/printer/Printer.hpp"
#include "core/command/motion/MotionCommands.hpp"
#include "core/command/extruder/ExtruderCommands.hpp"
#include "core/command/fan/FanCommands.hpp"
#include "core/command/system/SystemCommands.hpp"
#include "core/CommandExecutor.hpp"
#include <memory>
#include <vector>

namespace core {

/**
 * @brief Interfaccia principale del driver, esposta verso applicazioni esterne.
 */
    class DriverInterface {
    public:
        explicit DriverInterface(std::shared_ptr<Printer> printer, std::shared_ptr<SerialPort> serialPort);

        std::shared_ptr<command::motion::MotionCommands> motion() const;

        std::shared_ptr<command::extruder::ExtruderCommands> extruder() const;

        std::shared_ptr<command::fan::FanCommands> fan() const;

        std::shared_ptr<command::system::SystemCommands> system() const;

        types::Result sendCustomCommand(const std::string &rawCommand);

        PrintState getState() const;

        types::Result sendCommandInternal(char category, int code, const std::vector<std::string> &params);

    private:
        std::shared_ptr<Printer> printer_;
        std::shared_ptr<SerialPort> serialPort_;
        std::shared_ptr<CommandContext> commandContext_;
        std::shared_ptr<CommandExecutor> commandExecutor_;
        PrintState currentState_;

        std::shared_ptr<command::motion::MotionCommands> motion_;
        std::shared_ptr<command::extruder::ExtruderCommands> extruder_;
        std::shared_ptr<command::fan::FanCommands> fan_;
        std::shared_ptr<command::system::SystemCommands> system_;
    };

} // namespace core
