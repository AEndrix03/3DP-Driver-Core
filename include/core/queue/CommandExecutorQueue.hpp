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

namespace core {
    /**
     * @brief Command with priority for execution queue
     */
    struct PriorityCommand {
        std::string command;
        int priority;
        std::string jobId; // Optional, for future tracking
        // Lower priority value = higher priority execution (0 = highest)
        bool operator<(const PriorityCommand &other) const {
            return priority > other.priority; // Min-heap: lower values first
        }
    };

    /**
     * @brief Thread-safe command execution queue with priority support
     * Manages asynchronous command execution through GCodeTranslator
     */
    class CommandExecutorQueue {
    public:
        explicit CommandExecutorQueue(std::shared_ptr<translator::gcode::GCodeTranslator> translator);

        ~CommandExecutorQueue();

        /**
         * @brief Start the command processing thread
         */
        void start();

        /**
         * @brief Stop the command processing thread and clear queue
         */
        void stop();

        /**
         * @brief Check if queue processor is running
         */
        bool isRunning() const;

        /**
         * @brief Add single command to priority queue
         * @param command G-code command string
         * @param priority Command priority (higher = more urgent, default = 5)
         * @param jobId Optional job identifier for tracking
         */
        void enqueue(const std::string &command, int priority = 5, const std::string &jobId = "");

        /**
         * @brief Add all commands from G-code file to queue
         * @param filePath Path to G-code file
         * @param priority Priority for all commands in file (default = 5)
         * @param jobId Optional job identifier for all commands
         */
        void enqueueFile(const std::string &filePath, int priority = 5, const std::string &jobId = "");

        /**
         * @brief Add multiple commands from vector
         * @param commands Vector of G-code command strings
         * @param priority Priority for all commands (default = 5)
         * @param jobId Optional job identifier for all commands
         */
        void enqueueCommands(const std::vector<std::string> &commands, int priority = 5, const std::string &jobId = "");

        /**
         * @brief Get current queue size
         */
        size_t getQueueSize() const;

        /**
         * @brief Clear all pending commands
         */
        void clearQueue();

        /**
         * @brief Get execution statistics
         */
        struct Statistics {
            size_t totalEnqueued = 0;
            size_t totalExecuted = 0;
            size_t totalErrors = 0;
            size_t currentQueueSize = 0;
        };

        Statistics getStatistics() const;

    private:
        std::shared_ptr<translator::gcode::GCodeTranslator> translator_;
        // Thread-safe priority queue
        std::priority_queue<PriorityCommand> commandQueue_;
        mutable std::mutex queueMutex_;
        std::condition_variable queueCondition_;
        // Thread management
        std::thread processingThread_;
        std::atomic<bool> running_{false};
        std::atomic<bool> stopping_{false};
        // Statistics
        mutable Statistics stats_;
        mutable std::mutex statsMutex_;
        /**
                * @brief Main processing loop (runs in separate thread)
                */
        void processingLoop();

        /**
         * @brief Execute single command through translator
         */
        void executeCommand(const PriorityCommand &cmd);

        /**
         * @brief Update statistics thread-safely
         */
        void updateStats(bool executed, bool error);
    };
} // namespace core
