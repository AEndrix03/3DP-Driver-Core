//
// Created by Andrea on 23/08/2025.
//

#include "core/queue/CommandExecutorQueue.hpp"
#include "logger/Logger.hpp"
#include "translator/exceptions/GCodeTranslatorInvalidCommandException.hpp"
#include "translator/exceptions/GCodeTranslatorUnknownCommandException.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

#include "core/printer/job/tracking/JobTracker.hpp"
#include "core/printer/state/StateTracker.hpp"

namespace core {
    CommandExecutorQueue::CommandExecutorQueue(std::shared_ptr<translator::gcode::GCodeTranslator> translator)
        : translator_(std::move(translator)) {
        if (!translator_) {
            throw std::invalid_argument("GCodeTranslator cannot be null");
        }

        initDiskFile();
        Logger::logInfo("[CommandExecutorQueue] Initialized with paging support");
    }

    CommandExecutorQueue::~CommandExecutorQueue() {
        stop();
        closeDiskFile();
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
        }

        PriorityCommand cmd;
        cmd.command = command;
        cmd.priority = priority;
        cmd.jobId = jobId;
        cmd.sequenceId = nextSequenceId_.fetch_add(1); {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (stopping_) {
                Logger::logWarning("[CommandExecutorQueue] Rejecting command - queue is stopping");
                return;
            }

            if (needsPaging()) {
                pageCommandsToDisk();
            }

            commandQueue_.push(cmd);
            updateStats(false, false);
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

        // Single pass: count and collect commands
        while (std::getline(file, line)) {
            if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
            if (line[0] == ';' || line[0] == '%') continue;
            commands.push_back(line);
        }
        file.close();

        if (commands.empty()) {
            Logger::logWarning("[CommandExecutorQueue] No valid commands found in file: " + filePath);
            return;
        }

        // Initialize job tracking with real command count
        auto &jobTracker = core::jobs::JobTracker::getInstance();
        jobTracker.startJob(jobId, commands.size());

        // Reset state tracker for new job
        auto &stateTracker = core::state::StateTracker::getInstance();
        stateTracker.resetForNewJob();

        enqueueCommands(commands, priority, jobId);
        Logger::logInfo("[CommandExecutorQueue] Loaded " + std::to_string(commands.size()) +
                        " commands from file: " + filePath);
    }

    void CommandExecutorQueue::enqueueCommands(const std::vector<std::string> &commands, int priority,
                                               const std::string &jobId) {
        if (commands.empty()) {
            Logger::logWarning("[CommandExecutorQueue] Empty command vector ignored");
            return;
        }

        for (const auto &command: commands) {
            if (!command.empty() && command.find_first_not_of(" \t\r\n") != std::string::npos) {
                enqueue(command, priority, jobId);
            }
        }

        Logger::logInfo("[CommandExecutorQueue] Enqueued " + std::to_string(commands.size()) +
                        " commands (priority=" + std::to_string(priority) + ", jobId=" + jobId + ")");
    }

    size_t CommandExecutorQueue::getQueueSize() const {
        std::lock_guard<std::mutex> lock(queueMutex_);
        std::lock_guard<std::mutex> diskLock(diskMutex_);
        return commandQueue_.size() + diskQueue_.size();
    }

    void CommandExecutorQueue::clearQueue() {
        std::lock_guard<std::mutex> lock(queueMutex_);
        std::lock_guard<std::mutex> diskLock(diskMutex_);

        size_t clearedCount = commandQueue_.size() + diskQueue_.size();

        std::priority_queue<PriorityCommand> emptyQueue;
        commandQueue_.swap(emptyQueue);
        diskQueue_.clear();

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
            bool hasCommand = false; {
                std::unique_lock<std::mutex> lock(queueMutex_);

                if (commandQueue_.empty() && !diskQueue_.empty()) {
                    loadCommandsFromDisk();
                }

                queueCondition_.wait(lock, [this] {
                    return !commandQueue_.empty() || stopping_;
                });

                if (stopping_ && commandQueue_.empty()) {
                    std::lock_guard<std::mutex> diskLock(diskMutex_);
                    if (diskQueue_.empty()) {
                        break;
                    }
                }

                if (!commandQueue_.empty()) {
                    command = commandQueue_.top();
                    commandQueue_.pop();
                    hasCommand = true;
                }
            }

            if (hasCommand) {
                executeCommand(command);
            }
        }

        Logger::logInfo("[CommandExecutorQueue] Processing loop finished");
    }

    void CommandExecutorQueue::executeCommand(const PriorityCommand &cmd) {
        auto &tracker = jobs::JobTracker::getInstance();
        tracker.updateJobProgress(cmd.jobId, cmd.command);

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

    bool CommandExecutorQueue::needsPaging() const {
        return commandQueue_.size() >= MAX_COMMANDS_IN_RAM;
    }

    void CommandExecutorQueue::pageCommandsToDisk() {
        std::vector<PriorityCommand> commands;
        while (!commandQueue_.empty() && commandQueue_.size() > MAX_COMMANDS_IN_RAM / 2) {
            commands.push_back(commandQueue_.top());
            commandQueue_.pop();
        }

        std::sort(commands.begin(), commands.end());

        size_t keepInMemory = MAX_COMMANDS_IN_RAM / 2;
        for (size_t i = 0; i < std::min(keepInMemory, commands.size()); ++i) {
            commandQueue_.push(commands[i]);
        } {
            std::lock_guard<std::mutex> diskLock(diskMutex_);
            for (size_t i = keepInMemory; i < commands.size(); ++i) {
                diskQueue_.push_back(commands[i]);
                saveToDisk(commands[i]);
            }
            stats_.diskPagedCommands += (commands.size() - keepInMemory);
        }

        Logger::logInfo(
            "[CommandExecutorQueue] Paged " + std::to_string(commands.size() - keepInMemory) + " commands to disk");
    }

    void CommandExecutorQueue::loadCommandsFromDisk() {
        std::lock_guard<std::mutex> diskLock(diskMutex_);
        size_t loadCount = std::min(MAX_COMMANDS_IN_RAM / 4, diskQueue_.size());
        for (size_t i = 0; i < loadCount; ++i) {
            if (!diskQueue_.empty()) {
                commandQueue_.push(diskQueue_.front());
                diskQueue_.pop_front();
            }
        }

        if (loadCount > 0) {
            Logger::logInfo("[CommandExecutorQueue] Loaded " + std::to_string(loadCount) + " commands from disk");
        }
    }

    void CommandExecutorQueue::saveToDisk(const PriorityCommand &cmd) {
        if (diskFile_.is_open()) {
            diskFile_.write(reinterpret_cast<const char *>(&cmd.priority), sizeof(cmd.priority));
            diskFile_.write(reinterpret_cast<const char *>(&cmd.sequenceId), sizeof(cmd.sequenceId));
            size_t cmdSize = cmd.command.size();
            diskFile_.write(reinterpret_cast<const char *>(&cmdSize), sizeof(cmdSize));
            diskFile_.write(cmd.command.c_str(), cmdSize);

            size_t jobSize = cmd.jobId.size();
            diskFile_.write(reinterpret_cast<const char *>(&jobSize), sizeof(jobSize));
            diskFile_.write(cmd.jobId.c_str(), jobSize);

            diskFile_.flush();
            stats_.diskOperations++;
        }
    }

    bool CommandExecutorQueue::loadFromDisk(PriorityCommand &cmd) {
        if (!diskFile_.is_open()) return false;

        if (!diskFile_.read(reinterpret_cast<char *>(&cmd.priority), sizeof(cmd.priority))) return false;
        if (!diskFile_.read(reinterpret_cast<char *>(&cmd.sequenceId), sizeof(cmd.sequenceId))) return false;

        size_t cmdSize;
        if (!diskFile_.read(reinterpret_cast<char *>(&cmdSize), sizeof(cmdSize))) return false;
        cmd.command.resize(cmdSize);
        if (!diskFile_.read(&cmd.command[0], cmdSize)) return false;

        size_t jobSize;
        if (!diskFile_.read(reinterpret_cast<char *>(&jobSize), sizeof(jobSize))) return false;
        cmd.jobId.resize(jobSize);
        if (!diskFile_.read(&cmd.jobId[0], jobSize)) return false;

        stats_.diskOperations++;
        return true;
    }

    void CommandExecutorQueue::initDiskFile() {
        std::filesystem::create_directories("temp");
        diskFile_.open("temp/command_queue.dat", std::ios::binary | std::ios::in | std::ios::out | std::ios::app);
        if (!diskFile_.is_open()) {
            Logger::logWarning("[CommandExecutorQueue] Could not open disk file for paging");
        }
    }

    void CommandExecutorQueue::closeDiskFile() {
        if (diskFile_.is_open()) {
            diskFile_.close();
            std::filesystem::remove("temp/command_queue.dat");
        }
    }
} // namespace core
