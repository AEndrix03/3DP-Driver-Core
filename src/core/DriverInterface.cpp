//
// Created by redeg on 26/04/2025.
//

#include "core/DriverInterface.hpp"
#include "core/CommandBuilder.hpp"
#include "core/printer/ErrorRecovery.hpp"
#include "logger/Logger.hpp"
#include <queue>
#include <atomic>

namespace core {

    // Global command synchronization: define variable with external linkage so other translation units can use it.
    std::atomic<bool> g_commandInProgress{false};
    static std::mutex g_commandMutex;
    static std::condition_variable g_commandCV;

    DriverInterface::DriverInterface(std::shared_ptr<Printer> printer, std::shared_ptr<SerialPort> serialPort)
            : printer_(std::move(printer)),
              serialPort_(std::move(serialPort)),
              commandContext_(std::make_shared<CommandContext>()),
              commandExecutor_(std::make_shared<CommandExecutor>(serialPort_, commandContext_)),
              currentState_(PrintState::Idle),
              motion_(std::make_shared<command::motion::MotionCommands>(this)),
              endstop_(std::make_shared<command::endstop::EndstopCommands>(this)),
              extruder_(std::make_shared<command::extruder::ExtruderCommands>(this)),
              fan_(std::make_shared<command::fan::FanCommands>(this)),
              system_(std::make_shared<command::system::SystemCommands>(this)),
              history_(std::make_shared<command::history::HistoryCommands>(this)),
              temperature_(std::make_shared<command::temperature::TemperatureCommands>(this)) {
    }

    std::shared_ptr<command::motion::MotionCommands> DriverInterface::motion() const {
        return motion_;
    }

    std::shared_ptr<command::endstop::EndstopCommands> DriverInterface::endstop() const {
        return endstop_;
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

    std::shared_ptr<command::history::HistoryCommands> DriverInterface::history() const {
        return history_;
    }

    std::shared_ptr<command::temperature::TemperatureCommands> DriverInterface::temperature() const {
        return temperature_;
    }

    types::Result DriverInterface::sendCustomCommand(const std::string &rawCommand) const {
        if (!printer_->sendCommand(rawCommand)) {
            return {types::ResultCode::Error, "Failed to send custom command."};
        }
        return {types::ResultCode::Success, "Custom command sent."};
    }

    void DriverInterface::resendLastCommand() const {
        if (commandExecutor_) {
            commandExecutor_->resendLastCommand();
        }
    }

    PrintState DriverInterface::getState() const {
        return currentState_;
    }

    void DriverInterface::setState(PrintState newState) {
        if (currentState_ != newState) {
            Logger::logInfo("[DriverInterface] State change: " +
                            printStateToString(currentState_) + " -> " +
                            printStateToString(newState));
            currentState_ = newState;
        }
    }

    std::string DriverInterface::printStateToString(PrintState state) const {
        switch (state) {
            case PrintState::Idle:
                return "Idle";
            case PrintState::Homing:
                return "Homing";
            case PrintState::Printing:
                return "Printing";
            case PrintState::Paused:
                return "Paused";
            case PrintState::Error:
                return "Error";
            default:
                return "Unknown";
        }
    }

    types::Result
    DriverInterface::sendCommandInternal(char category, int code, const std::vector<std::string> &params) const {
        // CRITICAL: Wait if another command is in progress
        {
            std::unique_lock<std::mutex> lock(g_commandMutex);
            g_commandCV.wait(lock, []() { return !g_commandInProgress.load(); });
            g_commandInProgress = true;
        }

        // Ensure we release the lock on exit
        struct CommandGuard {
            ~CommandGuard() {
                g_commandInProgress = false;
                g_commandCV.notify_all();
            }
        } guard;

        // Get the next command number
        uint16_t cmdNum = commandContext_->nextCommandNumber();
        std::string command = CommandBuilder::buildCommand(cmdNum, category, code, params);

        // Update state based on command
        if (category == 'S') {
            if (code == 1) { // Start print
                const_cast<DriverInterface *>(this)->setState(PrintState::Printing);
            } else if (code == 2) { // Pause
                const_cast<DriverInterface *>(this)->setState(PrintState::Paused);
            } else if (code == 3) { // Resume
                const_cast<DriverInterface *>(this)->setState(PrintState::Printing);
            } else if (code == 0) { // Homing
                const_cast<DriverInterface *>(this)->setState(PrintState::Homing);
            }
        } else if (category == 'M' && code == 0) { // Emergency stop
            const_cast<DriverInterface *>(this)->setState(PrintState::Error);
        }

        // Send and wait for response
        types::Result result = commandExecutor_->sendCommandAndAwaitResponse(command, cmdNum);

        // If command failed with RESEND FAILED, we need to handle it
        if (result.message.find("RESEND FAILED") != std::string::npos) {
            Logger::logWarning("[DriverInterface] RESEND FAILED detected, continuing execution");
            // Don't treat as error, continue
            result.code = types::ResultCode::Success;
        }

        return result;
    }
} // namespace core

