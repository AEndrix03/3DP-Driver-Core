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
#include <functional>
#include <unordered_map>
#include <vector>

namespace core {

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

        bool isRunning() const {
            return running_.load() && !stopping_.load();
        }

        void start();

        void stop();

        void enqueue(const std::string &command, int priority = 5, const std::string &jobId = "");

        void enqueueFile(const std::string &filePath, int priority = 5, const std::string &jobId = "");

        void enqueueCommands(const std::vector<std::string> &commands, int priority = 5, const std::string &jobId = "");

        size_t getQueueSize() const;

        void clearQueue();

        /**
         * @brief Sospende/riprende l'esecuzione dei comandi di job (priorita' >= 3).
         * I comandi urgenti (priorita' < 3) continuano a passare. Nessun comando viene perso.
         */
        void setPaused(bool paused);

        bool isPaused() const { return paused_.load(); }

        /// Chiamata (dal thread della coda) quando un job viene fermato per un guasto.
        void setJobFaultCallback(std::function<void(const std::string &jobId, const std::string &reason)> callback);

        /// Chiamata (dal thread della coda) quando l'ultimo comando di un job e' stato eseguito.
        void setJobFinishedCallback(std::function<void(const std::string &jobId)> callback);

        struct Statistics {
            size_t totalEnqueued = 0;
            size_t totalExecuted = 0;
            size_t totalErrors = 0;
            size_t currentQueueSize = 0;
            size_t diskPagedCommands = 0;
            size_t diskOperations = 0;
        };

        Statistics getStatistics() const;

        /**
           * @brief Get queue condition variable for external notification
           * Used to wake up the processing thread from external sources
           */
        std::condition_variable &getQueueCondition() { return queueCondition_; }

        /**
         * @brief Force wake up the processing thread
         * Call this after enqueuing commands to ensure immediate processing
         */
        void wakeUp() {
            queueCondition_.notify_all();
        }

    private:
        std::shared_ptr<translator::gcode::GCodeTranslator> translator_;
        std::priority_queue<PriorityCommand> commandQueue_;      // 10k commands in RAM
        std::priority_queue<PriorityCommand> pagingBuffer_;      // 5k intermediate buffer
        std::deque<PriorityCommand> diskQueue_;                  // On-disk storage
        std::fstream diskFile_;
        mutable std::mutex queueMutex_;
        mutable std::mutex diskMutex_;
        std::condition_variable queueCondition_;
        std::thread processingThread_;
        std::thread healthThread_;                               // Health monitor thread
        std::atomic<bool> running_{false};
        std::atomic<bool> stopping_{false};
        std::atomic<bool> paused_{false};
        std::mutex lifecycleMutex_;

        // Comandi ancora da eseguire per ciascun job (protetto da queueMutex_)
        std::unordered_map<std::string, size_t> pendingByJob_;

        std::mutex callbackMutex_;
        std::function<void(const std::string &, const std::string &)> jobFaultCallback_;
        std::function<void(const std::string &)> jobFinishedCallback_;
        std::atomic<uint64_t> nextSequenceId_{1};

        // Health monitoring
        std::atomic<std::chrono::steady_clock::time_point> lastExecutionTime_;
        std::atomic<bool> executionStalled_{false};

        std::atomic<bool> processingThreadAlive_{false};
        std::atomic<std::thread::id> processingThreadId_;

        mutable Statistics stats_;
        mutable std::mutex statsMutex_;

        void processingLoop();

        void healthMonitorLoop();                                // Health check loop
        size_t getTotalCommandsAvailable() const;                // Get total pending commands
        void loadFromAllSources();

        void forceLoadFromDisk();                                // Force aggressive loading
        void flushPagingBufferToDisk();                          // Flush buffer to disk

        void closeDiskFile();

        void initDiskFile();

        bool loadFromDisk(PriorityCommand &cmd);

        void saveToDisk(const PriorityCommand &cmd);

        void updateStats(bool executed, bool error);

        void pageCommandsToDisk();

        void executeCommand(const PriorityCommand &cmd);

        /// Guasto durante un comando: svuota la coda e marca il job come fallito.
        void haltAfterFault(const std::string &jobId, const std::string &reason);

        /// Aggiorna il conteggio del job; ritorna true se era l'ultimo comando.
        bool markCommandDone(const std::string &jobId);

        void recoverFromStall();

        bool loadFromAllSourcesSafe();
    };
} // namespace core