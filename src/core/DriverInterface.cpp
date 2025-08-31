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

    // Global command synchronization with timeout support
    std::atomic<bool> g_commandInProgress{false};
    static std::mutex g_commandMutex;
    static std::condition_variable g_commandCV;
    static std::chrono::steady_clock::time_point g_lastCommandTime;

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
            // Force clear any stuck command
            g_commandInProgress = false;
            g_commandCV.notify_all();

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
        // CRITICAL: Wait with timeout to prevent deadlock
        const auto MAX_WAIT_TIME = std::chrono::seconds(30);
        const auto STALL_DETECTION_TIME = std::chrono::seconds(10);

        {
            std::unique_lock<std::mutex> lock(g_commandMutex);

            // Check for stalled command
            if (g_commandInProgress.load()) {
                auto now = std::chrono::steady_clock::now();
                auto timeSinceLastCommand = now - g_lastCommandTime;

                if (timeSinceLastCommand > STALL_DETECTION_TIME) {
                    Logger::logWarning("[DriverInterface] Command stalled for " +
                                       std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                               timeSinceLastCommand).count()) +
                                       "s, forcing recovery");

                    // Force clear the flag
                    g_commandInProgress = false;
                    g_commandCV.notify_all();
                }
            }

            // Wait with timeout
            bool acquired = g_commandCV.wait_for(lock, MAX_WAIT_TIME,
                                                 []() { return !g_commandInProgress.load(); });

            if (!acquired) {
                Logger::logError("[DriverInterface] Command wait timeout after 30s - forcing execution");
                // Force clear and continue
                g_commandInProgress = false;
                g_commandCV.notify_all();

                // Small delay to let other threads react
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            g_commandInProgress = true;
            g_lastCommandTime = std::chrono::steady_clock::now();
        }

        // RAII guard to ensure flag is always cleared
        struct CommandGuard {
            ~CommandGuard() {
                g_commandInProgress = false;
                g_commandCV.notify_all();
                Logger::logInfo("[DriverInterface] Command lock released");
            }
        } guard;

        try {
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

            // Send with timeout handling
            types::Result result = commandExecutor_->sendCommandAndAwaitResponse(command, cmdNum);

            // Check for timeout in result
            if (result.code == types::ResultCode::Timeout) {
                Logger::logError("[DriverInterface] Command timeout detected, clearing lock");
                // The guard will clear the lock
                return result;
            }

            // If command failed with RESEND FAILED, we need to handle it
            if (result.message.find("RESEND FAILED") != std::string::npos) {
                Logger::logWarning("[DriverInterface] RESEND FAILED detected, continuing execution");
                result.code = types::ResultCode::Success;
            }

            return result;

        } catch (const std::exception &e) {
            Logger::logError("[DriverInterface] Exception in sendCommandInternal: " + std::string(e.what()));
            // Guard will clear the lock
            return {types::ResultCode::Error, std::string("Exception: ") + e.what()};
        }
    }
} // namespace core
