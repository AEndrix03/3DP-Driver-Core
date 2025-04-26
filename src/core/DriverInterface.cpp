//
// Created by redeg on 26/04/2025.
//

#include "core/DriverInterface.hpp"
#include "core/CommandBuilder.hpp"

namespace core {

    DriverInterface::DriverInterface(std::shared_ptr<Printer> printer, std::shared_ptr<SerialPort> serialPort)
            : printer_(std::move(printer)),
              serialPort_(std::move(serialPort)),
              commandContext_(std::make_shared<CommandContext>()),
              commandExecutor_(std::make_shared<CommandExecutor>(serialPort_, commandContext_)),
              currentState_(PrintState::Idle),
              motion_(std::make_shared<command::motion::MotionCommands>(this)),
              extruder_(std::make_shared<command::extruder::ExtruderCommands>(this)),
              fan_(std::make_shared<command::fan::FanCommands>(this)),
              system_(std::make_shared<command::system::SystemCommands>(this)) {}

    std::shared_ptr<command::motion::MotionCommands> DriverInterface::motion() const {
        return motion_;
    }

    std::shared_ptr<command::extruder::ExtruderCommands> DriverInterface::extruder() const {
        return extruder_;
    }

    std::shared_ptr<command::fan::FanCommands> DriverInterface::fan() const {
        return fan_;
    }

    std::shared_ptr<command::system::SystemCommands> DriverInterface::system() const {
        return system_;
    }

    types::Result DriverInterface::sendCustomCommand(const std::string &rawCommand) {
        if (!printer_->sendCommand(rawCommand)) {
            return {types::ResultCode::Error, "Failed to send custom command."};
        }
        return {types::ResultCode::Success, "Custom command sent."};
    }

    PrintState DriverInterface::getState() const {
        return currentState_;
    }

    types::Result
    DriverInterface::sendCommandInternal(char category, int code, const std::vector<std::string> &params) {
        uint16_t cmdNum = commandContext_->nextCommandNumber();
        std::string command = CommandBuilder::buildCommand(cmdNum, category, code, params);
        commandContext_->storeCommand(cmdNum, command);

        if (!printer_->sendCommand(command)) {
            currentState_ = PrintState::Error;
            return {types::ResultCode::Error, "Failed to send command."};
        }

        auto result = commandExecutor_->sendCommandAndAwaitResponse(command, cmdNum);

        if (result.isSuccess()) {
            currentState_ = PrintState::Running;
        } else {
            currentState_ = PrintState::Error;
        }

        return result;
    }

} // namespace core
