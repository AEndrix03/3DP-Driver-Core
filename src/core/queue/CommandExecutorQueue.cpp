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
    static constexpr size_t MAX_COMMANDS_IN_RAM = 10000;
    static constexpr size_t PAGING_BUFFER_SIZE = 5000;
    static constexpr size_t RELOAD_THRESHOLD = 100;
    static constexpr size_t RELOAD_BATCH_SIZE = 1000;

    CommandExecutorQueue::CommandExecutorQueue(std::shared_ptr<translator::gcode::GCodeTranslator> translator)
            : translator_(std::move(translator)), running_(false), stopping_(false),
              lastExecutionTime_(std::chrono::steady_clock::now()) {
        if (!translator_) {
            throw std::invalid_argument("GCodeTranslator cannot be null");
        }
        initDiskFile();
        Logger::logInfo("[CommandExecutorQueue] Created with advanced paging system");
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

        Logger::logInfo("[CommandExecutorQueue] Starting executor");
        running_ = true;
        stopping_ = false;
        lastExecutionTime_ = std::chrono::steady_clock::now();
        executionStalled_ = false;

        // Start processing thread with proper exception handling
        processingThread_ = std::thread([this]() {
            try {
                processingLoop();
            } catch (const std::exception &e) {
                Logger::logError("[CommandExecutorQueue] Processing thread crashed: " + std::string(e.what()));
                running_ = false;
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

        Logger::logInfo("[CommandExecutorQueue] Started successfully");
    }

    void CommandExecutorQueue::stop() {
        if (!running_) return;

        Logger::logInfo("[CommandExecutorQueue] Stopping...");

        // CRITICAL: Set stopping flag first to prevent new commands
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            stopping_ = true;
            running_ = false;
        }

        // Wake up all waiting threads
        queueCondition_.notify_all();

        // Wait for threads to finish with timeout
        if (processingThread_.joinable()) {
            processingThread_.join();
        }
        if (healthThread_.joinable()) {
            healthThread_.join();
        }

        clearQueue();
        Logger::logInfo("[CommandExecutorQueue] Stopped");
    }

    void CommandExecutorQueue::processingLoop() {
        Logger::logInfo("[CommandExecutorQueue] Processing loop started");
        processingThreadAlive_ = true;
        processingThreadId_ = std::this_thread::get_id();

        size_t executedCount = 0;
        size_t executedSinceReload = 0;
        auto lastLogTime = std::chrono::steady_clock::now();

        try {
            while (running_) {
                processingThreadAlive_ = true; // Heartbeat

                PriorityCommand command;
                bool hasCommand = false;

                // FIXED: Use consistent mutex ordering
                {
                    std::unique_lock<std::mutex> lock(queueMutex_);

                    // Check if stopping
                    if (stopping_) break;

                    // Progressive reload logic with timeout
                    if (executedSinceReload >= RELOAD_THRESHOLD) {
                        if (!loadFromAllSourcesSafe()) {
                            Logger::logWarning("[CommandExecutorQueue] Load from sources failed - continuing");
                        }
                        executedSinceReload = 0;
                    }

                    // Force reload if queue is low but commands available
                    if (commandQueue_.size() < RELOAD_BATCH_SIZE) {
                        loadFromAllSourcesSafe();
                    }

                    // Wait for commands if queue is empty
                    if (commandQueue_.empty()) {
                        // Use timeout to prevent infinite wait
                        auto waitResult = queueCondition_.wait_for(lock, std::chrono::milliseconds(500));
                        if (waitResult == std::cv_status::timeout) {
                            // Periodic check - continue loop
                            continue;
                        }
                    }

                    if (commandQueue_.empty()) continue;

                    // Get next command
                    command = commandQueue_.top();
                    commandQueue_.pop();
                    hasCommand = true;
                    executedSinceReload++;
                }

                if (hasCommand) {
                    try {
                        executeCommand(command);
                        executedCount++;

                        // Update health tracking
                        lastExecutionTime_ = std::chrono::steady_clock::now();

                        // Progress logging
                        if (executedCount % 100 == 0) {
                            auto now = std::chrono::steady_clock::now();
                            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastLogTime).count();
                            Logger::logInfo("[CommandExecutorQueue] Executed: " + std::to_string(executedCount) +
                                            ", Rate: " + std::to_string(elapsed > 0 ? 100 / elapsed : 0) + " cmd/s");
                            lastLogTime = now;
                        }

                    } catch (const std::exception &e) {
                        Logger::logError("[CommandExecutorQueue] Command execution failed: " + std::string(e.what()));
                        // Continue processing other commands
                    }
                }

                // Small delay to prevent busy waiting
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } catch (const std::exception &e) {
            Logger::logError("[CommandExecutorQueue] Processing loop crashed: " + std::string(e.what()));
            processingThreadAlive_ = false;
            running_ = false;
            throw; // Re-throw to be caught by health monitor
        }

        processingThreadAlive_ = false;
        Logger::logInfo(
                "[CommandExecutorQueue] Processing loop finished. Total executed: " + std::to_string(executedCount));
    }

    void CommandExecutorQueue::healthMonitorLoop() {
        Logger::logInfo("[CommandExecutorQueue] Health monitor started");

        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (!running_) break;

            auto now = std::chrono::steady_clock::now();
            auto timeSinceLastExecution = std::chrono::duration_cast<std::chrono::seconds>(
                    now - lastExecutionTime_.load()).count();

            size_t totalCommands = getTotalCommandsAvailable();

            // Check if processing thread is alive
            bool threadAlive = processingThreadAlive_.load();

            // Detect stall condition
            bool isStalled = totalCommands > 0 && (timeSinceLastExecution > 15 || !threadAlive);

            if (isStalled && !executionStalled_) {
                executionStalled_ = true;
                Logger::logError("[CommandExecutorQueue] STALL DETECTED! " +
                                 std::to_string(timeSinceLastExecution) + "s since last execution, " +
                                 "thread alive: " + (threadAlive ? "true" : "false"));

                // AGGRESSIVE RECOVERY STRATEGY
                if (!threadAlive || timeSinceLastExecution > 60) {
                    Logger::logError("[CommandExecutorQueue] CRITICAL: Processing thread dead or hung - RESTARTING");

                    // Force restart processing thread
                    restartProcessingThread();
                } else {
                    // Standard recovery
                    recoverFromStall();
                }

                executionStalled_ = false;
            }

            // Periodic status log
            static int statusCounter = 0;
            if (++statusCounter % 12 == 0 && totalCommands > 0) {
                Logger::logInfo("[CommandExecutorQueue] Health: " + std::to_string(totalCommands) +
                                " commands pending, last exec " + std::to_string(timeSinceLastExecution) +
                                "s ago, thread alive: " + (threadAlive ? "true" : "false"));
            }
        }

        Logger::logInfo("[CommandExecutorQueue] Health monitor stopped");
    }

    void CommandExecutorQueue::executeCommand(const PriorityCommand &cmd) {
        auto &tracker = jobs::JobTracker::getInstance();
        tracker.updateJobProgress(cmd.jobId, cmd.command);

        // Skip comments and empty lines
        if (cmd.command.empty() || cmd.command[0] == ';' || cmd.command[0] == '%') {
            updateStats(true, false);
            return;
        }

        // Log critical commands
        bool shouldLog = (cmd.priority <= 2) ||
                         (cmd.command.find("M24") != std::string::npos) ||  // Start print
                         (cmd.command.find("M25") != std::string::npos) ||  // Pause
                         (cmd.command.find("M112") != std::string::npos) || // Emergency stop
                         (cmd.command.find("G28") != std::string::npos);    // Homing

        if (shouldLog) {
            Logger::logInfo("[CommandExecutorQueue] EXECUTING: " + cmd.command +
                            " (priority=" + std::to_string(cmd.priority) + ", jobId=" + cmd.jobId + ")");
        }

        try {
            // FIXED: Direct execution without complex timeout/threading
            translator_->parseLine(cmd.command);
            updateStats(true, false);

            if (shouldLog) {
                Logger::logInfo("[CommandExecutorQueue] Command executed successfully");
            }

        } catch (const GCodeTranslatorInvalidCommandException &e) {
            updateStats(false, true);
            Logger::logWarning("[CommandExecutorQueue] Invalid G-code: " + cmd.command + " - " + std::string(e.what()));
        } catch (const GCodeTranslatorUnknownCommandException &e) {
            updateStats(false, true);
            Logger::logWarning("[CommandExecutorQueue] Unknown G-code: " + cmd.command + " - " + std::string(e.what()));
        } catch (const std::exception &e) {
            updateStats(false, true);
            Logger::logError(
                    "[CommandExecutorQueue] Execution error for '" + cmd.command + "': " + std::string(e.what()));
        }
    }

    // Rest of the methods remain the same but with FIXED locking order
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
        // FIXED: Only call when already holding queueMutex_
        size_t currentSize = commandQueue_.size();
        if (currentSize >= RELOAD_BATCH_SIZE) return;

        size_t toLoad = std::min(RELOAD_BATCH_SIZE, MAX_COMMANDS_IN_RAM - currentSize);
        size_t loadedFromBuffer = 0;

        // Load from paging buffer first
        while (!pagingBuffer_.empty() && loadedFromBuffer < toLoad) {
            commandQueue_.push(pagingBuffer_.top());
            pagingBuffer_.pop();
            loadedFromBuffer++;
        }

        // Load from disk if needed
        if (loadedFromBuffer < toLoad) {
            std::lock_guard<std::mutex> diskLock(diskMutex_);
            size_t diskToLoad = toLoad - loadedFromBuffer;
            size_t loadedFromDisk = 0;

            while (!diskQueue_.empty() && loadedFromDisk < diskToLoad) {
                commandQueue_.push(diskQueue_.front());
                diskQueue_.pop_front();
                loadedFromDisk++;
            }

            if (loadedFromBuffer > 0 || loadedFromDisk > 0) {
                Logger::logInfo("[CommandExecutorQueue] Loaded " + std::to_string(loadedFromBuffer) +
                                " from buffer, " + std::to_string(loadedFromDisk) + " from disk");
            }
        }
    }

    void CommandExecutorQueue::forceLoadFromDisk() {
        // FIXED: Only call when already holding queueMutex_
        while (!pagingBuffer_.empty() && commandQueue_.size() < MAX_COMMANDS_IN_RAM) {
            commandQueue_.push(pagingBuffer_.top());
            pagingBuffer_.pop();
        }

        {
            std::lock_guard<std::mutex> diskLock(diskMutex_);
            size_t loaded = 0;
            while (!diskQueue_.empty() && commandQueue_.size() < MAX_COMMANDS_IN_RAM && loaded < RELOAD_BATCH_SIZE) {
                commandQueue_.push(diskQueue_.front());
                diskQueue_.pop_front();
                loaded++;
            }
            if (loaded > 0) {
                Logger::logInfo(
                        "[CommandExecutorQueue] Force loaded " + std::to_string(loaded) + " commands from disk");
            }
        }
    }

    void CommandExecutorQueue::enqueue(const std::string &command, int priority, const std::string &jobId) {
        if (command.empty() || command.find_first_not_of(" \t\r\n") == std::string::npos) {
            return;
        }

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

            // High priority goes directly to RAM
            if (priority < 3) {
                commandQueue_.push(cmd);
                Logger::logInfo("[CommandExecutorQueue] High priority command enqueued");
            } else {
                // Normal priority follows standard flow
                if (commandQueue_.size() < MAX_COMMANDS_IN_RAM) {
                    commandQueue_.push(cmd);
                } else if (pagingBuffer_.size() < PAGING_BUFFER_SIZE) {
                    pagingBuffer_.push(cmd);
                } else {
                    flushPagingBufferToDisk();
                    pagingBuffer_.push(cmd);
                }
            }
            updateStats(false, false);
        }

        queueCondition_.notify_all();
    }

    void CommandExecutorQueue::enqueueCommands(const std::vector<std::string> &commands, int priority,
                                               const std::string &jobId) {
        if (commands.empty()) return;

        Logger::logInfo("[CommandExecutorQueue] Enqueuing " + std::to_string(commands.size()) +
                        " commands with priority " + std::to_string(priority));

        if (!running_) {
            start();
        }

        size_t enqueuedCount = 0;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            for (const auto &command: commands) {
                if (!command.empty() && command.find_first_not_of(" \t\r\n") != std::string::npos) {
                    PriorityCommand cmd;
                    cmd.command = command;
                    cmd.priority = priority;
                    cmd.jobId = jobId;
                    cmd.sequenceId = nextSequenceId_.fetch_add(1);

                    if (commandQueue_.size() < MAX_COMMANDS_IN_RAM) {
                        commandQueue_.push(cmd);
                    } else if (pagingBuffer_.size() < PAGING_BUFFER_SIZE) {
                        pagingBuffer_.push(cmd);
                    } else {
                        flushPagingBufferToDisk();
                        pagingBuffer_.push(cmd);
                    }
                    enqueuedCount++;
                }
            }
            stats_.totalEnqueued += enqueuedCount;
        }

        Logger::logInfo("[CommandExecutorQueue] Successfully enqueued " + std::to_string(enqueuedCount) + " commands");

        // Wake up processing thread
        for (int i = 0; i < 5; ++i) {
            queueCondition_.notify_all();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void CommandExecutorQueue::enqueueFile(const std::string &filePath, int priority, const std::string &jobId) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            Logger::logError("[CommandExecutorQueue] Cannot open file: " + filePath);
            return;
        }

        std::vector<std::string> commands;
        std::string line;
        size_t validCommands = 0;

        while (std::getline(file, line)) {
            if (!line.empty() && line.find_first_not_of(" \t\r\n") != std::string::npos &&
                line[0] != ';' && line[0] != '%') {
                commands.push_back(line);
                validCommands++;
            }
        }
        file.close();

        Logger::logInfo("[CommandExecutorQueue] File loaded: " + std::to_string(validCommands) + " commands");

        if (commands.empty()) {
            Logger::logWarning("[CommandExecutorQueue] No valid commands in file");
            return;
        }

        // Initialize job tracking
        auto &jobTracker = core::jobs::JobTracker::getInstance();
        jobTracker.startJob(jobId, commands.size());

        auto &stateTracker = core::state::StateTracker::getInstance();
        stateTracker.resetForNewJob();

        enqueueCommands(commands, priority, jobId);
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
            Logger::logInfo("[CommandExecutorQueue] Cleared " + std::to_string(clearedCount) + " commands");
        }
    }

    CommandExecutorQueue::Statistics CommandExecutorQueue::getStatistics() const {
        std::lock_guard<std::mutex> lock(statsMutex_);
        Statistics result = stats_;
        result.currentQueueSize = getTotalCommandsAvailable();
        return result;
    }

    void CommandExecutorQueue::flushPagingBufferToDisk() {
        if (pagingBuffer_.empty()) return;

        std::lock_guard<std::mutex> diskLock(diskMutex_);
        size_t flushedCount = 0;

        while (!pagingBuffer_.empty()) {
            PriorityCommand cmd = pagingBuffer_.top();
            pagingBuffer_.pop();
            diskQueue_.push_back(cmd);
            saveToDisk(cmd);
            flushedCount++;
        }

        stats_.diskPagedCommands += flushedCount;
        stats_.diskOperations++;
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

        if (std::filesystem::exists(diskPath)) {
            std::filesystem::remove(diskPath);
        }

        diskFile_.open(diskPath, std::ios::binary | std::ios::in | std::ios::out | std::ios::app);
        if (!diskFile_.is_open()) {
            Logger::logWarning("[CommandExecutorQueue] Could not open disk file");
        } else {
            Logger::logInfo("[CommandExecutorQueue] Disk file initialized: " + diskPath);
        }
    }

    void CommandExecutorQueue::closeDiskFile() {
        if (diskFile_.is_open()) {
            diskFile_.close();
            std::filesystem::remove("temp/command_queue.dat");
        }
    }

    void CommandExecutorQueue::restartProcessingThread() {
        Logger::logError("[CommandExecutorQueue] Forcing processing thread restart");

        // Stop current thread if still joinable
        running_ = false;
        queueCondition_.notify_all();

        if (processingThread_.joinable()) {
            try {
                processingThread_.join();
                Logger::logInfo("[CommandExecutorQueue] Old processing thread joined");
            } catch (const std::exception &e) {
                Logger::logError("[CommandExecutorQueue] Error joining old thread: " + std::string(e.what()));
            }
        }

        // Reset state
        running_ = true;
        stopping_ = false;
        executionStalled_ = false;
        processingThreadAlive_ = false;
        lastExecutionTime_ = std::chrono::steady_clock::now();

        // Start new processing thread
        processingThread_ = std::thread([this]() {
            try {
                processingLoop();
            } catch (const std::exception &e) {
                Logger::logError(
                        "[CommandExecutorQueue] Restarted processing thread crashed: " + std::string(e.what()));
                processingThreadAlive_ = false;
                running_ = false;
            }
        });

        Logger::logInfo("[CommandExecutorQueue] Processing thread restarted successfully");
    }

    void CommandExecutorQueue::recoverFromStall() {
        Logger::logInfo("[CommandExecutorQueue] Attempting standard stall recovery");

        // Clear any potential deadlock by aggressive loading
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            loadFromAllSourcesSafe();
        }

        // Wake up processing thread multiple times
        for (int i = 0; i < 10; ++i) {
            queueCondition_.notify_all();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    bool CommandExecutorQueue::loadFromAllSourcesSafe() {
        try {
            // FIXED: Acquire locks in consistent order to prevent deadlock
            size_t currentSize = commandQueue_.size();
            if (currentSize >= RELOAD_BATCH_SIZE) return true;

            size_t toLoad = std::min(RELOAD_BATCH_SIZE, MAX_COMMANDS_IN_RAM - currentSize);
            size_t loadedFromBuffer = 0;

            // Load from paging buffer first (no additional locking needed)
            while (!pagingBuffer_.empty() && loadedFromBuffer < toLoad) {
                commandQueue_.push(pagingBuffer_.top());
                pagingBuffer_.pop();
                loadedFromBuffer++;
            }

            // Load from disk if needed (avoid deadlock with timeout approach)
            if (loadedFromBuffer < toLoad) {
                bool diskLockAcquired = false;
                auto startTime = std::chrono::steady_clock::now();

                // Try to acquire disk lock with manual timeout
                while (!diskLockAcquired &&
                       (std::chrono::steady_clock::now() - startTime) < std::chrono::milliseconds(50)) {
                    std::unique_lock<std::mutex> diskLock(diskMutex_, std::try_to_lock);
                    if (diskLock.owns_lock()) {
                        diskLockAcquired = true;

                        size_t diskToLoad = toLoad - loadedFromBuffer;
                        size_t loadedFromDisk = 0;

                        while (!diskQueue_.empty() && loadedFromDisk < diskToLoad) {
                            commandQueue_.push(diskQueue_.front());
                            diskQueue_.pop_front();
                            loadedFromDisk++;
                        }

                        if (loadedFromBuffer > 0 || loadedFromDisk > 0) {
                            Logger::logInfo("[CommandExecutorQueue] Loaded " + std::to_string(loadedFromBuffer) +
                                            " from buffer, " + std::to_string(loadedFromDisk) + " from disk");
                        }
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }

                if (!diskLockAcquired) {
                    Logger::logWarning("[CommandExecutorQueue] Disk lock timeout - skipping disk load");
                    return false;
                }
            }

            return true;
        } catch (const std::exception &e) {
            Logger::logError("[CommandExecutorQueue] loadFromAllSourcesSafe failed: " + std::string(e.what()));
            return false;
        }
    }

} // namespace core