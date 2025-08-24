//
// Created by Andrea on 23/08/2025.
//

#pragma once

#include "translator/GCodeTranslator.hpp"
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <string>
#include <deque>
#include <fstream>

namespace core {
    static constexpr size_t MAX_COMMANDS_IN_RAM = 2000;

    struct PriorityCommand {
        std::string command;
        int priority;
        std::string jobId;
        uint64_t sequenceId;

        bool operator<(const PriorityCommand &other) const {
            if (priority != other.priority) {
                return priority > other.priority; // Lower number = higher priority
            }
            return sequenceId > other.sequenceId; // FIFO within same priority
        }
    };

    class CommandExecutorQueue {
    public:
        explicit CommandExecutorQueue(std::shared_ptr<translator::gcode::GCodeTranslator> translator);

        ~CommandExecutorQueue();

        void start();

        void stop();

        bool isRunning() const;

        void enqueue(const std::string &command, int priority = 5, const std::string &jobId = "");

        void enqueueFile(const std::string &filePath, int priority = 5, const std::string &jobId = "");

        void enqueueCommands(const std::vector<std::string> &commands, int priority = 5, const std::string &jobId = "");

        size_t getQueueSize() const;

        void clearQueue();

        struct Statistics {
            size_t totalEnqueued = 0;
            size_t totalExecuted = 0;
            size_t totalErrors = 0;
            size_t currentQueueSize = 0;
            size_t diskPagedCommands = 0;
            size_t diskOperations = 0;
        };

        Statistics getStatistics() const;

    private:
        std::shared_ptr<translator::gcode::GCodeTranslator> translator_;
        std::priority_queue<PriorityCommand> commandQueue_;
        std::deque<PriorityCommand> diskQueue_;
        std::fstream diskFile_;
        mutable std::mutex queueMutex_;
        mutable std::mutex diskMutex_;
        std::condition_variable queueCondition_;
        std::thread processingThread_;
        std::atomic<bool> running_{false};
        std::atomic<bool> stopping_{false};
        std::atomic<uint64_t> nextSequenceId_{1};

        mutable Statistics stats_;
        mutable std::mutex statsMutex_;

        void processingLoop();

        void executeCommand(const PriorityCommand &cmd);

        void updateStats(bool executed, bool error);

        void pageCommandsToDisk();

        void loadCommandsFromDisk();

        bool needsPaging() const;

        void saveToDisk(const PriorityCommand &cmd);

        bool loadFromDisk(PriorityCommand &cmd);

        void initDiskFile();

        void closeDiskFile();
    };
} // namespace core
