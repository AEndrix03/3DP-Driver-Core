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
#include <chrono>
#include <optional>
#include "core/printer/job/JobCheckpoint.hpp"
#include <functional>
#include <unordered_map>
#include <vector>

namespace core {

    struct PriorityCommand {
        std::string command;
        int priority;
        std::string jobId;
        uint64_t sequenceId;
        int64_t fileLine = -1; ///< indice della riga nel file G-code del job (-1 = non appartiene a un file)

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

        /**
         * @brief Accoda un file G-code come job tracciato (con checkpoint per la ripresa).
         * @param startLine prima riga del file da eseguire (ripresa da checkpoint)
         * @param preamble comandi da eseguire prima del file (ripristino dello stato)
         * @param endGCode G-code da eseguire dopo l'ultima riga del file
         */
        void enqueueFile(const std::string &filePath, int priority = 5, const std::string &jobId = "",
                         size_t startLine = 0, const std::vector<std::string> &preamble = {},
                         const std::string &endGCode = "");

        /// Scrive subito su disco il checkpoint del job in corso (es. prima di una pausa).
        void saveCheckpointNow();

        /// Smette di tracciare il job ed elimina il suo checkpoint (job annullato).
        void forgetJob(const std::string &jobId);

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

        // --- checkpoint del job in corso (usati solo dal thread della coda, tranne dove indicato) ---
        struct LineRecord {
            print::JobCheckpoint start;  // stato all'inizio della riga
            uint32_t lastCommand;        // ultimo comando firmware generato dalla riga
            uint32_t sequenceEpoch;
            bool issued;                 // false finche' la riga non e' stata inviata per intero
        };
        std::mutex checkpointMutex_;
        std::string trackedJobId_;
        std::string trackedPath_;
        std::string trackedEndGCode_;
        std::deque<LineRecord> inFlight_;
        std::optional<print::JobCheckpoint> pendingCheckpoint_;
        std::chrono::steady_clock::time_point lastCheckpointSave_{};

        print::JobCheckpoint captureState(int64_t fileLine) const;

        void beginLine(const PriorityCommand &cmd);

        void endLine(const PriorityCommand &cmd, bool faulted);

        void persistCheckpoint(bool force);

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