//
// Created by Andrea on 23/08/2025.
//

#include "core/queue/CommandExecutorQueue.hpp"
#include "logger/Logger.hpp"
#include "translator/exceptions/GCodeTranslatorInvalidCommandException.hpp"
#include "translator/exceptions/GCodeTranslatorUnknownCommandException.hpp"
#include <fstream>
#include <sstream>

namespace core {
    CommandExecutorQueue::CommandExecutorQueue(std::shared_ptr<translator::gcode::GCodeTranslator> translator)
        : translator_(std::move(translator)) {
        if (!translator_) {
            throw std::invalid_argument("GCodeTranslator cannot be null");
        }

        Logger::logInfo("[CommandExecutorQueue] Initialized");
    }

    CommandExecutorQueue::~CommandExecutorQueue() {
        stop();
    }

    void CommandExecutorQueue::start() {
        if (running_) {
            Logger::logWarning("[CommandExecutorQueue] Already running");
            return;
        }

        running_ = true;
        stopping_ = false;

        processingThread_ = std::thread([this]() {
            try {
                processingLoop();
            } catch (const std::exception &e) {
                Logger::logError("[CommandExecutorQueue] Processing thread crashed: " + std::string(e.what()));
            }
        });

        Logger::logInfo("[CommandExecutorQueue] Started processing thread");
    }

    void CommandExecutorQueue::stop() {
        if (!running_) return;

        Logger::logInfo("[CommandExecutorQueue] Stopping..."); {
            std::lock_guard<std::mutex> lock(queueMutex_);
            stopping_ = true;
            running_ = false;
        }

        queueCondition_.notify_all();

        if (processingThread_.joinable()) {
            processingThread_.join();
        }

        clearQueue();
        Logger::logInfo("[CommandExecutorQueue] Stopped");
    }

    bool CommandExecutorQueue::isRunning() const {
        return running_;
    }

    void CommandExecutorQueue::enqueue(const std::string &command, int priority, const std::string &jobId) {
        if (command.empty() || command.find_first_not_of(" \t\r\n") == std::string::npos) {
            Logger::logWarning("[CommandExecutorQueue] Empty command ignored");
            return;
        } {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (stopping_) {
                Logger::logWarning("[CommandExecutorQueue] Rejecting command - queue is stopping");
                return;
            }

            commandQueue_.emplace(PriorityCommand{command, priority, jobId});
            updateStats(false, false); // Just enqueued
        }

        queueCondition_.notify_one();

        Logger::logInfo("[CommandExecutorQueue] Enqueued command (priority=" + std::to_string(priority) +
                        ", jobId=" + jobId + "): " + command);
    }

    void CommandExecutorQueue::enqueueFile(const std::string &filePath, int priority, const std::string &jobId) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            Logger::logError("[CommandExecutorQueue] Cannot open file: " + filePath);
            return;
        }

        Logger::logInfo("[CommandExecutorQueue] Loading G-code file: " + filePath);

        std::vector<std::string> commands;
        std::string line;

        while (std::getline(file, line)) {
            // Skip empty lines and comments
            if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
            if (line[0] == ';' || line[0] == '%') continue;

            commands.push_back(line);
        }

        file.close();

        if (commands.empty()) {
            Logger::logWarning("[CommandExecutorQueue] No valid commands found in file: " + filePath);
            return;
        }

        enqueueCommands(commands, priority, jobId);
        Logger::logInfo("[CommandExecutorQueue] Loaded " + std::to_string(commands.size()) +
                        " commands from file: " + filePath);
    }

    void CommandExecutorQueue::enqueueCommands(const std::vector<std::string> &commands, int priority,
                                               const std::string &jobId) {
        if (commands.empty()) {
            Logger::logWarning("[CommandExecutorQueue] Empty command vector ignored");
            return;
        } {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (stopping_) {
                Logger::logWarning("[CommandExecutorQueue] Rejecting commands - queue is stopping");
                return;
            }

            for (const auto &command: commands) {
                if (!command.empty() && command.find_first_not_of(" \t\r\n") != std::string::npos) {
                    commandQueue_.emplace(PriorityCommand{command, priority, jobId});
                    stats_.totalEnqueued++;
                }
            }
        }

        queueCondition_.notify_all();

        Logger::logInfo("[CommandExecutorQueue] Enqueued " + std::to_string(commands.size()) +
                        " commands (priority=" + std::to_string(priority) + ", jobId=" + jobId + ")");
    }

    size_t CommandExecutorQueue::getQueueSize() const {
        std::lock_guard<std::mutex> lock(queueMutex_);
        return commandQueue_.size();
    }

    void CommandExecutorQueue::clearQueue() {
        std::lock_guard<std::mutex> lock(queueMutex_);

        size_t clearedCount = commandQueue_.size();

        // Clear the priority queue by swapping with empty queue
        std::priority_queue<PriorityCommand> emptyQueue;
        commandQueue_.swap(emptyQueue);

        if (clearedCount > 0) {
            Logger::logInfo("[CommandExecutorQueue] Cleared " + std::to_string(clearedCount) + " pending commands");
        }
    }

    CommandExecutorQueue::Statistics CommandExecutorQueue::getStatistics() const {
        std::lock_guard<std::mutex> lock(statsMutex_);
        Statistics result = stats_;
        result.currentQueueSize = getQueueSize();
        return result;
    }

    void CommandExecutorQueue::processingLoop() {
        Logger::logInfo("[CommandExecutorQueue] Processing loop started");

        while (running_) {
            PriorityCommand command;

            // Wait for commands or stop signal
            {
                std::unique_lock<std::mutex> lock(queueMutex_);

                queueCondition_.wait(lock, [this] {
                    return !commandQueue_.empty() || stopping_;
                });

                if (stopping_ && commandQueue_.empty()) {
                    break;
                }

                if (commandQueue_.empty()) {
                    continue;
                }

                // Get highest priority command
                command = commandQueue_.top();
                commandQueue_.pop();
            }

            // Execute command outside of lock
            executeCommand(command);
        }

        Logger::logInfo("[CommandExecutorQueue] Processing loop finished");
    }

    void CommandExecutorQueue::executeCommand(const PriorityCommand &cmd) {
        Logger::logInfo("[CommandExecutorQueue] Executing (priority=" + std::to_string(cmd.priority) +
                        ", jobId=" + cmd.jobId + "): " + cmd.command);

        try {
            translator_->parseLine(cmd.command);
            updateStats(true, false);

            Logger::logInfo("[CommandExecutorQueue] Command executed successfully: " + cmd.command);
        } catch (const GCodeTranslatorInvalidCommandException &e) {
            updateStats(false, true);
            Logger::logError("[CommandExecutorQueue] Invalid G-code command: " + std::string(e.what()));
        } catch (const GCodeTranslatorUnknownCommandException &e) {
            updateStats(false, true);
            Logger::logError("[CommandExecutorQueue] Unknown G-code command: " + std::string(e.what()));
        } catch (const std::exception &e) {
            updateStats(false, true);
            Logger::logError("[CommandExecutorQueue] Execution error: " + std::string(e.what()));
        }
    }

    void CommandExecutorQueue::updateStats(bool executed, bool error) {
        std::lock_guard<std::mutex> lock(statsMutex_);

        if (!executed && !error) {
            stats_.totalEnqueued++;
        } else if (executed) {
            stats_.totalExecuted++;
        } else if (error) {
            stats_.totalErrors++;
        }
    }
} // namespace core
