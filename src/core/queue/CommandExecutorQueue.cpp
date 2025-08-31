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
    // Configurazione migliorata per la paginazione
    static constexpr size_t MAX_COMMANDS_IN_RAM = 10000;     // 10k comandi in RAM
    static constexpr size_t PAGING_BUFFER_SIZE = 5000;       // 5k buffer pre-paging
    static constexpr size_t RELOAD_THRESHOLD = 100;          // Ricarica ogni 100 comandi eseguiti (1%)
    static constexpr size_t RELOAD_BATCH_SIZE = 1000;        // Ricarica 1000 comandi alla volta (10%)

    CommandExecutorQueue::CommandExecutorQueue(std::shared_ptr<translator::gcode::GCodeTranslator> translator)
            : translator_(std::move(translator)), running_(false), stopping_(false),
              lastExecutionTime_(std::chrono::steady_clock::now()) {
        if (!translator_) {
            throw std::invalid_argument("GCodeTranslator cannot be null");
        }

        initDiskFile();

        Logger::logInfo("===============================================");
        Logger::logInfo("[CommandExecutorQueue] COMMAND EXECUTOR QUEUE CREATED");
        Logger::logInfo("===============================================");
        Logger::logInfo("[CommandExecutorQueue] Configuration:");
        Logger::logInfo("  Max RAM commands: " + std::to_string(MAX_COMMANDS_IN_RAM));
        Logger::logInfo("  Pre-paging buffer: " + std::to_string(PAGING_BUFFER_SIZE));
        Logger::logInfo("  Reload threshold: every " + std::to_string(RELOAD_THRESHOLD) + " commands");
        Logger::logInfo("  Reload batch size: " + std::to_string(RELOAD_BATCH_SIZE) + " commands");
        Logger::logInfo("[CommandExecutorQueue] Ready to receive and execute commands");
        Logger::logInfo("===============================================");
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

        Logger::logInfo("===============================================");
        Logger::logInfo("[CommandExecutorQueue] STARTING COMMAND EXECUTOR");
        Logger::logInfo("===============================================");

        running_ = true;
        stopping_ = false;

        // Reset health tracking
        lastExecutionTime_ = std::chrono::steady_clock::now();
        executionStalled_ = false;

        Logger::logInfo("[CommandExecutorQueue] Configuration:");
        Logger::logInfo("  Max RAM commands: " + std::to_string(MAX_COMMANDS_IN_RAM));
        Logger::logInfo("  Paging buffer size: " + std::to_string(PAGING_BUFFER_SIZE));
        Logger::logInfo("  Reload threshold: " + std::to_string(RELOAD_THRESHOLD));
        Logger::logInfo("  Reload batch size: " + std::to_string(RELOAD_BATCH_SIZE));

        Logger::logInfo("[CommandExecutorQueue] Initial queue status:");
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            Logger::logInfo("  RAM Queue: " + std::to_string(commandQueue_.size()));
            Logger::logInfo("  Paging Buffer: " + std::to_string(pagingBuffer_.size()));
        }
        {
            std::lock_guard<std::mutex> diskLock(diskMutex_);
            Logger::logInfo("  Disk Queue: " + std::to_string(diskQueue_.size()));
        }

        // Start processing thread
        processingThread_ = std::thread([this]() {
            try {
                processingLoop();
            } catch (const std::exception &e) {
                Logger::logError("[CommandExecutorQueue] Processing thread crashed: " + std::string(e.what()));
            }
        });

        // Start health monitor thread
        healthThread_ = std::thread([this]() {
            try {
                healthMonitorLoop();
            } catch (const std::exception &e) {
                Logger::logError("[CommandExecutorQueue] Health thread crashed: " + std::string(e.what()));
            }
        });

        Logger::logInfo("[CommandExecutorQueue] Started processing and health threads");
        Logger::logInfo("===============================================");
        Logger::logInfo("[CommandExecutorQueue] READY TO EXECUTE COMMANDS");
        Logger::logInfo("===============================================");
    }

    void CommandExecutorQueue::stop() {
        if (!running_) return;

        Logger::logInfo("[CommandExecutorQueue] Stopping...");
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            stopping_ = true;
            running_ = false;
        }

        queueCondition_.notify_all();

        if (processingThread_.joinable()) {
            processingThread_.join();
        }

        if (healthThread_.joinable()) {
            healthThread_.join();
        }

        clearQueue();
        Logger::logInfo("[CommandExecutorQueue] Stopped");
    }

    bool CommandExecutorQueue::isRunning() const {
        return running_;
    }

    void CommandExecutorQueue::processingLoop() {
        Logger::logInfo("[CommandExecutorQueue] Processing loop started - ALWAYS ACTIVE");

        size_t executedCount = 0;
        size_t executedSinceReload = 0;
        size_t consecutiveErrors = 0;
        auto lastLogTime = std::chrono::steady_clock::now();

        while (running_) {
            PriorityCommand command;
            bool hasCommand = false;

            {
                std::unique_lock<std::mutex> lock(queueMutex_);

                // Progressive reload logic
                if (executedSinceReload >= RELOAD_THRESHOLD) {
                    loadFromAllSources();
                    executedSinceReload = 0;
                }

                // Force reload if queue is low
                if (commandQueue_.size() < RELOAD_BATCH_SIZE && getTotalCommandsAvailable() > 0) {
                    Logger::logInfo("[CommandExecutorQueue] Queue low (" +
                                    std::to_string(commandQueue_.size()) + "), reloading...");
                    loadFromAllSources();
                }

                // Aggressive reload if completely empty
                if (commandQueue_.empty()) {
                    size_t totalAvailable = getTotalCommandsAvailable();
                    if (totalAvailable > 0) {
                        Logger::logWarning("[CommandExecutorQueue] Queue empty but " +
                                           std::to_string(totalAvailable) + " commands available");
                        forceLoadFromDisk();

                        // If still empty, there's a problem
                        if (commandQueue_.empty()) {
                            Logger::logError("[CommandExecutorQueue] Failed to load commands!");
                            // Don't get stuck, continue checking
                            queueCondition_.wait_for(lock, std::chrono::milliseconds(100));
                            continue;
                        }
                    }
                }

                // Wait only if truly no commands
                if (commandQueue_.empty()) {
                    queueCondition_.wait_for(lock, std::chrono::milliseconds(100));
                    continue; // Check again
                }

                // Get next command
                if (!commandQueue_.empty()) {
                    command = commandQueue_.top();
                    commandQueue_.pop();
                    hasCommand = true;
                    executedSinceReload++;

                    // Update health tracking
                    lastExecutionTime_ = std::chrono::steady_clock::now();
                    executionStalled_ = false;
                    consecutiveErrors = 0; // Reset error counter

                    // Log high priority commands
                    if (command.priority < 3) {
                        Logger::logInfo("[CommandExecutorQueue] High priority command dequeued");
                    }
                }
            }

            if (hasCommand) {
                // Execute with error recovery
                try {
                    executeCommand(command);
                    executedCount++;
                } catch (const std::exception &e) {
                    consecutiveErrors++;
                    Logger::logError("[CommandExecutorQueue] Command execution exception: " +
                                     std::string(e.what()));

                    // If too many consecutive errors, pause briefly
                    if (consecutiveErrors > 5) {
                        Logger::logError("[CommandExecutorQueue] Too many errors, pausing 1s");
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                        consecutiveErrors = 0;
                    }
                }

                // Progress logging
                if (executedCount % 100 == 0) {
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastLogTime).count();

                    size_t totalRemaining = getTotalCommandsAvailable();
                    size_t queueSize = 0;
                    {
                        std::lock_guard<std::mutex> lock(queueMutex_);
                        queueSize = commandQueue_.size();
                    }

                    Logger::logInfo("[CommandExecutorQueue] Progress: " + std::to_string(executedCount) +
                                    " executed, RAM=" + std::to_string(queueSize) +
                                    ", Remaining=" + std::to_string(totalRemaining) +
                                    ", Rate=" + std::to_string(elapsed > 0 ? 100 / elapsed : 0) + " cmd/s");
                    lastLogTime = now;
                }
            } else {
                // No command - very brief pause
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        Logger::logInfo("[CommandExecutorQueue] Processing loop finished. Total executed: " +
                        std::to_string(executedCount));
    }

    void CommandExecutorQueue::healthMonitorLoop() {
        Logger::logInfo("[CommandExecutorQueue] Health monitor started");

        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(2));

            if (!running_) break;

            auto now = std::chrono::steady_clock::now();
            auto timeSinceLastExecution = std::chrono::duration_cast<std::chrono::seconds>(
                    now - lastExecutionTime_.load()).count();

            size_t totalCommands = getTotalCommandsAvailable();
            size_t queueSize = 0;
            size_t bufferSize = 0;
            size_t diskSize = 0;

            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                queueSize = commandQueue_.size();
                bufferSize = pagingBuffer_.size();
            }
            {
                std::lock_guard<std::mutex> diskLock(diskMutex_);
                diskSize = diskQueue_.size();
            }

            // Check if command is being executed
            extern std::atomic<bool> g_commandInProgress;
            bool commandActive = g_commandInProgress.load();

            // Detect stall condition
            bool isStalled = totalCommands > 0 && !commandActive && timeSinceLastExecution > 10;

            if (isStalled && !executionStalled_) {
                executionStalled_ = true;
                Logger::logError("[CommandExecutorQueue] STALL DETECTED! Queue=" + std::to_string(queueSize) +
                                 ", Buffer=" + std::to_string(bufferSize) +
                                 ", Disk=" + std::to_string(diskSize) +
                                 ", " + std::to_string(timeSinceLastExecution) + "s since last execution");

                // Strategia di recovery
                if (queueSize == 0 && (bufferSize > 0 || diskSize > 0)) {
                    // Il problema è nel caricamento - forza il reload
                    Logger::logWarning("[CommandExecutorQueue] Forcing aggressive reload from disk/buffer");
                    {
                        std::lock_guard<std::mutex> lock(queueMutex_);
                        forceLoadFromDisk();
                    }
                } else if (queueSize > 0) {
                    // Comandi in coda ma non eseguiti - prova a resendare l'ultimo comando
                    Logger::logWarning("[CommandExecutorQueue] Attempting recovery by resending last command");
                    if (translator_ && translator_->getDriver()) {
                        translator_->getDriver()->resendLastCommand();
                    }
                }

                // Wake up processing thread aggressively
                for (int i = 0; i < 20; ++i) {
                    queueCondition_.notify_all();
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }

                // Reset after recovery attempt
                std::this_thread::sleep_for(std::chrono::seconds(2));
                executionStalled_ = false;
            } else if (commandActive) {
                executionStalled_ = false;
            }

            // Log periodic status
            static int statusCounter = 0;
            if (++statusCounter % 5 == 0 && totalCommands > 0) {  // Every 10 seconds
                Logger::logInfo("[CommandExecutorQueue] Health Status: Queue=" +
                                std::to_string(queueSize) +
                                ", Buffer=" + std::to_string(bufferSize) +
                                ", Disk=" + std::to_string(diskSize) +
                                ", LastExec=" + std::to_string(timeSinceLastExecution) + "s ago");
            }
        }

        Logger::logInfo("[CommandExecutorQueue] Health monitor stopped");
    }

    size_t CommandExecutorQueue::getTotalCommandsAvailable() const {
        size_t ramAndBuffer = 0;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            ramAndBuffer = commandQueue_.size() + pagingBuffer_.size();
        }

        size_t disk = 0;
        {
            std::lock_guard<std::mutex> diskLock(diskMutex_);
            disk = diskQueue_.size();
        }

        return ramAndBuffer + disk;
    }

    void CommandExecutorQueue::loadFromAllSources() {
        size_t loadedFromBuffer = 0;
        size_t loadedFromDisk = 0;

        // Obiettivo: mantenere sempre RELOAD_BATCH_SIZE comandi pronti in RAM
        size_t currentSize = commandQueue_.size();

        if (currentSize >= RELOAD_BATCH_SIZE) {
            return;  // Abbiamo abbastanza comandi
        }

        size_t toLoad = std::min(RELOAD_BATCH_SIZE, MAX_COMMANDS_IN_RAM - currentSize);

        // Prima carica dal buffer di pre-paging (già ordinato per priorità)
        while (!pagingBuffer_.empty() && loadedFromBuffer < toLoad) {
            commandQueue_.push(pagingBuffer_.top());
            pagingBuffer_.pop();
            loadedFromBuffer++;
        }

        // Se servono ancora comandi, carica dal disco
        if (loadedFromBuffer < toLoad) {
            std::lock_guard<std::mutex> diskLock(diskMutex_);
            size_t diskToLoad = toLoad - loadedFromBuffer;

            // Carica in batch per efficienza
            std::vector<PriorityCommand> batch;
            while (!diskQueue_.empty() && batch.size() < diskToLoad) {
                batch.push_back(diskQueue_.front());
                diskQueue_.pop_front();
                loadedFromDisk++;
            }

            // Aggiungi alla coda principale
            for (const auto &cmd: batch) {
                commandQueue_.push(cmd);
            }
        }

        if (loadedFromBuffer > 0 || loadedFromDisk > 0) {
            Logger::logInfo("[CommandExecutorQueue] Loaded " +
                            std::to_string(loadedFromBuffer) + " from buffer, " +
                            std::to_string(loadedFromDisk) + " from disk. " +
                            "Queue now has " + std::to_string(commandQueue_.size()) + " commands ready");
        }
    }

    void CommandExecutorQueue::forceLoadFromDisk() {
        Logger::logWarning("[CommandExecutorQueue] Force loading from all sources");

        // Svuota tutto il buffer nel queue principale
        while (!pagingBuffer_.empty() && commandQueue_.size() < MAX_COMMANDS_IN_RAM) {
            commandQueue_.push(pagingBuffer_.top());
            pagingBuffer_.pop();
        }

        // Carica aggressivamente dal disco
        {
            std::lock_guard<std::mutex> diskLock(diskMutex_);
            size_t loaded = 0;
            while (!diskQueue_.empty() && commandQueue_.size() < MAX_COMMANDS_IN_RAM) {
                commandQueue_.push(diskQueue_.front());
                diskQueue_.pop_front();
                loaded++;

                if (loaded >= RELOAD_BATCH_SIZE) {
                    break;  // Non caricare tutto in una volta
                }
            }

            if (loaded > 0) {
                Logger::logInfo("[CommandExecutorQueue] Force loaded " + std::to_string(loaded) +
                                " commands from disk");
            }
        }
    }

    void CommandExecutorQueue::enqueue(const std::string &command, int priority, const std::string &jobId) {
        if (command.empty() || command.find_first_not_of(" \t\r\n") == std::string::npos) {
            return;
        }

        // Auto-start if not running
        if (!running_) {
            Logger::logInfo("[CommandExecutorQueue] Auto-starting queue for incoming command");
            start();
        }

        PriorityCommand cmd;
        cmd.command = command;
        cmd.priority = priority;
        cmd.jobId = jobId;
        cmd.sequenceId = nextSequenceId_.fetch_add(1);

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (stopping_) {
                Logger::logWarning("[CommandExecutorQueue] Rejecting command - queue is stopping");
                return;
            }

            // Se alta priorità (< 3), vai direttamente in RAM queue
            if (priority < 3) {
                commandQueue_.push(cmd);
                Logger::logInfo("[CommandExecutorQueue] High priority command enqueued directly to RAM");
            } else {
                // Normale priorità - segui il flusso standard
                if (commandQueue_.size() < MAX_COMMANDS_IN_RAM) {
                    commandQueue_.push(cmd);
                } else if (pagingBuffer_.size() < PAGING_BUFFER_SIZE) {
                    pagingBuffer_.push(cmd);
                } else {
                    // Buffer pieno - flush su disco e aggiungi al buffer svuotato
                    flushPagingBufferToDisk();
                    pagingBuffer_.push(cmd);
                }
            }

            updateStats(false, false);
        }

        queueCondition_.notify_all();
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

        size_t totalLines = 0;
        size_t commentLines = 0;
        size_t emptyLines = 0;
        size_t validCommands = 0;

        while (std::getline(file, line)) {
            totalLines++;

            if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) {
                emptyLines++;
                continue;
            }
            if (line[0] == ';' || line[0] == '%') {
                commentLines++;
                continue;
            }

            commands.push_back(line);
            validCommands++;
        }
        file.close();

        Logger::logInfo("[CommandExecutorQueue] File parsing complete:");
        Logger::logInfo("  Total lines: " + std::to_string(totalLines));
        Logger::logInfo("  Valid commands: " + std::to_string(validCommands));
        Logger::logInfo("  Comment lines: " + std::to_string(commentLines));
        Logger::logInfo("  Empty lines: " + std::to_string(emptyLines));

        if (commands.empty()) {
            Logger::logWarning("[CommandExecutorQueue] No valid commands found in file: " + filePath);
            return;
        }

        // Initialize job tracking
        auto &jobTracker = core::jobs::JobTracker::getInstance();
        jobTracker.startJob(jobId, commands.size());

        auto &stateTracker = core::state::StateTracker::getInstance();
        stateTracker.resetForNewJob();

        // Always ensure processing is running
        if (!running_) {
            Logger::logInfo("[CommandExecutorQueue] Starting queue for file processing");
            start();
        }

        enqueueCommands(commands, priority, jobId);

        // Aggressive wake-up
        for (int i = 0; i < 10; ++i) {
            queueCondition_.notify_all();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        Logger::logInfo("[CommandExecutorQueue] File enqueued successfully, processing active");
    }

    void CommandExecutorQueue::enqueueCommands(const std::vector<std::string> &commands, int priority,
                                               const std::string &jobId) {
        if (commands.empty()) return;

        Logger::logInfo("[CommandExecutorQueue] Received request to enqueue " + std::to_string(commands.size()) +
                        " commands with priority " + std::to_string(priority) + " for job " + jobId);

        // Auto-start if not running
        if (!running_) {
            Logger::logInfo("[CommandExecutorQueue] Auto-starting queue for incoming commands");
            start();
        }

        size_t enqueuedCount = 0;
        std::lock_guard<std::mutex> lock(queueMutex_);

        for (const auto &command: commands) {
            if (!command.empty() && command.find_first_not_of(" \t\r\n") != std::string::npos) {
                PriorityCommand cmd;
                cmd.command = command;
                cmd.priority = priority;
                cmd.jobId = jobId;
                cmd.sequenceId = nextSequenceId_.fetch_add(1);

                // Distribuzione intelligente tra RAM, buffer e disco
                if (commandQueue_.size() < MAX_COMMANDS_IN_RAM) {
                    commandQueue_.push(cmd);
                } else if (pagingBuffer_.size() < PAGING_BUFFER_SIZE) {
                    pagingBuffer_.push(cmd);
                } else {
                    // Flush del buffer su disco quando pieno
                    if (pagingBuffer_.size() >= PAGING_BUFFER_SIZE) {
                        flushPagingBufferToDisk();
                    }
                    pagingBuffer_.push(cmd);
                }

                enqueuedCount++;

                // Log di progresso ogni 10k comandi
                if (enqueuedCount % 10000 == 0) {
                    Logger::logInfo("[CommandExecutorQueue] Enqueued " + std::to_string(enqueuedCount) +
                                    " commands so far...");
                }
            }
        }

        // Flush finale del buffer se ci sono comandi rimanenti
        if (pagingBuffer_.size() > 0 && commandQueue_.size() >= MAX_COMMANDS_IN_RAM) {
            Logger::logInfo("[CommandExecutorQueue] Final flush of paging buffer (" +
                            std::to_string(pagingBuffer_.size()) + " commands)");
            flushPagingBufferToDisk();
        }

        updateStats(false, false);
        stats_.totalEnqueued += enqueuedCount;

        Logger::logInfo("[CommandExecutorQueue] Successfully enqueued " + std::to_string(enqueuedCount) +
                        " commands (priority=" + std::to_string(priority) + ", jobId=" + jobId + ")");
        Logger::logInfo("[CommandExecutorQueue] Distribution: RAM=" + std::to_string(commandQueue_.size()) +
                        ", Buffer=" + std::to_string(pagingBuffer_.size()) +
                        ", Disk=" + std::to_string(diskQueue_.size()));

        // Wake up processing thread multiple times to ensure it starts
        Logger::logInfo("[CommandExecutorQueue] Notifying processing thread...");
        for (int i = 0; i < 5; ++i) {
            queueCondition_.notify_all();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Logger::logInfo("[CommandExecutorQueue] Processing thread notified");
    }

    size_t CommandExecutorQueue::getQueueSize() const {
        return getTotalCommandsAvailable();
    }

    void CommandExecutorQueue::clearQueue() {
        std::lock_guard<std::mutex> lock(queueMutex_);
        std::lock_guard<std::mutex> diskLock(diskMutex_);

        size_t clearedCount = commandQueue_.size() + pagingBuffer_.size() + diskQueue_.size();

        std::priority_queue<PriorityCommand> emptyQueue;
        commandQueue_.swap(emptyQueue);

        std::priority_queue<PriorityCommand> emptyBuffer;
        pagingBuffer_.swap(emptyBuffer);

        diskQueue_.clear();

        if (clearedCount > 0) {
            Logger::logInfo("[CommandExecutorQueue] Cleared " + std::to_string(clearedCount) + " pending commands");
        }
    }

    void CommandExecutorQueue::executeCommand(const PriorityCommand &cmd) {
        auto &tracker = jobs::JobTracker::getInstance();
        tracker.updateJobProgress(cmd.jobId, cmd.command);

        // Always log critical commands and periodically log others
        static size_t logCounter = 0;
        bool shouldLog = (cmd.priority <= 2) || (++logCounter % 100 == 0);

        // ALWAYS log critical commands
        if (cmd.command.find("M24") != std::string::npos ||  // Start print
            cmd.command.find("M25") != std::string::npos ||  // Pause
            cmd.command.find("M112") != std::string::npos || // Emergency stop
            cmd.command.find("G28") != std::string::npos ||  // Homing
            cmd.command.find("M104") != std::string::npos || // Set hotend temp
            cmd.command.find("M140") != std::string::npos) { // Set bed temp
            shouldLog = true;
        }

        if (shouldLog) {
            Logger::logInfo("[CommandExecutorQueue] EXECUTING: " + cmd.command +
                            " (priority=" + std::to_string(cmd.priority) +
                            ", jobId=" + cmd.jobId +
                            ", seq=" + std::to_string(cmd.sequenceId) + ")");
        }

        try {
            // Check if the command is a comment or empty
            if (cmd.command.empty() || cmd.command[0] == ';' || cmd.command[0] == '%') {
                // Skip comments and empty lines
                updateStats(true, false);
                return;
            }

            // Log before parsing for debugging
            if (shouldLog) {
                Logger::logInfo("[CommandExecutorQueue] Sending to translator: " + cmd.command);
            }

            // Set a timeout for command execution
            auto startTime = std::chrono::steady_clock::now();
            const auto COMMAND_TIMEOUT = std::chrono::seconds(20);

            // Execute in a separate thread with timeout
            std::atomic<bool> commandComplete{false};
            std::atomic<bool> commandSuccess{false};
            std::string errorMessage;

            std::thread commandThread([this, &cmd, &commandComplete, &commandSuccess, &errorMessage]() {
                try {
                    translator_->parseLine(cmd.command);
                    commandSuccess = true;
                } catch (const GCodeTranslatorInvalidCommandException &e) {
                    errorMessage = "Invalid G-code: " + std::string(e.what());
                    commandSuccess = false;
                } catch (const GCodeTranslatorUnknownCommandException &e) {
                    errorMessage = "Unknown G-code: " + std::string(e.what());
                    commandSuccess = false;
                } catch (const std::exception &e) {
                    errorMessage = "Execution error: " + std::string(e.what());
                    commandSuccess = false;
                }
                commandComplete = true;
            });

            // Wait for command with timeout
            bool timedOut = false;
            while (!commandComplete && !timedOut) {
                auto elapsed = std::chrono::steady_clock::now() - startTime;
                if (elapsed > COMMAND_TIMEOUT) {
                    timedOut = true;
                    Logger::logError("[CommandExecutorQueue] Command timeout after 20s: " + cmd.command);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            // Detach thread if it's still running (timeout case)
            if (timedOut && commandThread.joinable()) {
                commandThread.detach();

                // Force clear any stuck command lock in driver
                if (translator_ && translator_->getDriver()) {
                    translator_->getDriver()->resendLastCommand();
                }

                updateStats(false, true);
                Logger::logError("[CommandExecutorQueue] Command abandoned due to timeout: " + cmd.command);

                // Continue processing other commands
                return;
            }

            // Join thread if it completed
            if (commandThread.joinable()) {
                commandThread.join();
            }

            // Handle result
            if (commandSuccess) {
                updateStats(true, false);
                if (shouldLog) {
                    Logger::logInfo("[CommandExecutorQueue] Command executed successfully: " + cmd.command);
                }
            } else {
                updateStats(false, true);
                Logger::logWarning("[CommandExecutorQueue] Command failed: " + cmd.command +
                                   " - Error: " + errorMessage);
            }

        } catch (const std::exception &e) {
            updateStats(false, true);
            Logger::logError("[CommandExecutorQueue] Unexpected error for '" + cmd.command +
                             "': " + std::string(e.what()));
        }
    }

    void CommandExecutorQueue::pageCommandsToDisk() {
        // Questo metodo ora è sostituito da flushPagingBufferToDisk
        flushPagingBufferToDisk();
    }

    void CommandExecutorQueue::flushPagingBufferToDisk() {
        if (pagingBuffer_.empty()) {
            return;
        }

        std::lock_guard<std::mutex> diskLock(diskMutex_);
        size_t flushedCount = 0;

        // Flush completo del buffer su disco
        while (!pagingBuffer_.empty()) {
            PriorityCommand cmd = pagingBuffer_.top();
            pagingBuffer_.pop();
            diskQueue_.push_back(cmd);
            saveToDisk(cmd);
            flushedCount++;
        }

        stats_.diskPagedCommands += flushedCount;
        stats_.diskOperations++;

        Logger::logInfo("[CommandExecutorQueue] Flushed " + std::to_string(flushedCount) +
                        " commands to disk. Total on disk: " + std::to_string(diskQueue_.size()));
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

    CommandExecutorQueue::Statistics CommandExecutorQueue::getStatistics() const {
        std::lock_guard<std::mutex> lock(statsMutex_);
        Statistics result = stats_;
        result.currentQueueSize = getTotalCommandsAvailable();
        return result;
    }

    void CommandExecutorQueue::saveToDisk(const PriorityCommand &cmd) {
        if (diskFile_.is_open()) {
            // Salva con formato binario per efficienza
            diskFile_.write(reinterpret_cast<const char *>(&cmd.priority), sizeof(cmd.priority));
            diskFile_.write(reinterpret_cast<const char *>(&cmd.sequenceId), sizeof(cmd.sequenceId));

            size_t cmdSize = cmd.command.size();
            diskFile_.write(reinterpret_cast<const char *>(&cmdSize), sizeof(cmdSize));
            diskFile_.write(cmd.command.c_str(), cmdSize);

            size_t jobSize = cmd.jobId.size();
            diskFile_.write(reinterpret_cast<const char *>(&jobSize), sizeof(jobSize));
            diskFile_.write(cmd.jobId.c_str(), jobSize);

            diskFile_.flush();
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
        std::string diskPath = "temp/command_queue.dat";

        // Rimuovi il file precedente se esiste
        if (std::filesystem::exists(diskPath)) {
            std::filesystem::remove(diskPath);
        }

        diskFile_.open(diskPath, std::ios::binary | std::ios::in | std::ios::out | std::ios::app);
        if (!diskFile_.is_open()) {
            Logger::logWarning("[CommandExecutorQueue] Could not open disk file for paging");
        } else {
            Logger::logInfo("[CommandExecutorQueue] Disk paging file initialized: " + diskPath);
        }
    }

    void CommandExecutorQueue::closeDiskFile() {
        if (diskFile_.is_open()) {
            diskFile_.close();
            std::filesystem::remove("temp/command_queue.dat");
            Logger::logInfo("[CommandExecutorQueue] Disk paging file closed and removed");
        }
    }

} // namespace core