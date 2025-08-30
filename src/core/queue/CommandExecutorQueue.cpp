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

    CommandExecutorQueue::CommandExecutorQueue(std::shared_ptr<translator::gcode::GCodeTranslator> translator)
            : translator_(std::move(translator)), running_(false), stopping_(false),
              lastExecutionTime_(std::chrono::steady_clock::now()) {
        if (!translator_) {
            throw std::invalid_argument("GCodeTranslator cannot be null");
        }

        initDiskFile();
        Logger::logInfo("[CommandExecutorQueue] Initialized with enhanced paging and health monitoring");
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

        // Reset health tracking
        lastExecutionTime_ = std::chrono::steady_clock::now();
        executionStalled_ = false;

        Logger::logInfo("[CommandExecutorQueue] Starting with queue status:");
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
        Logger::logInfo("[CommandExecutorQueue] Processing loop started");

        size_t executedCount = 0;
        auto lastLogTime = std::chrono::steady_clock::now();

        while (running_) {
            PriorityCommand command;
            bool hasCommand = false;

            {
                std::unique_lock<std::mutex> lock(queueMutex_);

                // Always try to load from disk/buffer first
                if (commandQueue_.size() < 100) {  // Keep at least 100 ready
                    loadFromAllSources();
                }

                // Check total commands available
                size_t totalAvailable = getTotalCommandsAvailable();

                if (commandQueue_.empty() && totalAvailable > 0) {
                    Logger::logWarning("[CommandExecutorQueue] Main queue empty but " +
                                       std::to_string(totalAvailable) + " commands available elsewhere");
                    loadFromAllSources();
                }

                // If still empty, wait briefly
                if (commandQueue_.empty()) {
                    queueCondition_.wait_for(lock, std::chrono::milliseconds(50));

                    // Try one more time after wait
                    if (commandQueue_.empty() && totalAvailable > 0) {
                        loadFromAllSources();
                    }
                }

                // Check for stop condition
                if (stopping_ && commandQueue_.empty()) {
                    std::lock_guard<std::mutex> diskLock(diskMutex_);
                    if (diskQueue_.empty() && pagingBuffer_.empty()) {
                        break;
                    }
                }

                // Get next command
                if (!commandQueue_.empty()) {
                    command = commandQueue_.top();
                    commandQueue_.pop();
                    hasCommand = true;

                    // Update health tracking
                    lastExecutionTime_ = std::chrono::steady_clock::now();
                    executionStalled_ = false;
                }
            }

            if (hasCommand) {
                executeCommand(command);
                executedCount++;

                // Progress logging
                if (executedCount % 100 == 0) {
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastLogTime).count();

                    size_t totalRemaining = getTotalCommandsAvailable();
                    Logger::logInfo("[CommandExecutorQueue] Progress: " + std::to_string(executedCount) +
                                    " executed, " + std::to_string(totalRemaining) + " remaining, " +
                                    "rate: " + std::to_string(elapsed > 0 ? 100 / elapsed : 0) + " cmd/s");
                    lastLogTime = now;
                }
            } else {
                // No command available - brief pause
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

            // Check if execution is truly stalled (not just BUSY)
            auto now = std::chrono::steady_clock::now();
            auto timeSinceLastExecution = std::chrono::duration_cast<std::chrono::seconds>(
                    now - lastExecutionTime_.load()).count();

            size_t totalCommands = getTotalCommandsAvailable();

            // Check if command is being executed (global flag from DriverInterface)
            extern std::atomic<bool> g_commandInProgress;
            bool commandActive = g_commandInProgress.load();

            // Only consider it stalled if:
            // 1. We have commands pending
            // 2. No command is currently executing
            // 3. Significant time has passed
            if (totalCommands > 0 && !commandActive && timeSinceLastExecution > 10) {
                Logger::logWarning("[CommandExecutorQueue] HEALTH CHECK: TRUE STALL DETECTED! " +
                                   std::to_string(totalCommands) + " commands pending, " +
                                   std::to_string(timeSinceLastExecution) + "s since last execution");

                // Force recovery by resending last command
                if (!executionStalled_) {
                    executionStalled_ = true;
                    Logger::logWarning("[CommandExecutorQueue] Attempting recovery by resending last command...");

                    // Resend the last command through the driver
                    // This should trigger a DUPLICATE response and unblock the system
                    if (translator_ && translator_->getDriver()) {
                        translator_->getDriver()->resendLastCommand();
                        Logger::logInfo("[CommandExecutorQueue] Resent last command, expecting DUPLICATE response");
                    }

                    // Wait for the DUPLICATE to be processed
                    std::this_thread::sleep_for(std::chrono::seconds(2));

                    // Wake processing thread to continue
                    for (int i = 0; i < 10; ++i) {
                        queueCondition_.notify_all();
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }

                    // Reset stall flag after recovery attempt
                    executionStalled_ = false;
                }
            } else if (commandActive) {
                // Reset stall flag if command is active
                executionStalled_ = false;
            }

            // Log periodic status
            if (totalCommands > 0 && timeSinceLastExecution > 0) {
                static int statusCounter = 0;
                if (++statusCounter % 5 == 0) {  // Every 10 seconds
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    Logger::logInfo("[CommandExecutorQueue] Health Status: Queue=" +
                                    std::to_string(commandQueue_.size()) +
                                    ", Buffer=" + std::to_string(pagingBuffer_.size()) +
                                    ", Disk=" + std::to_string(diskQueue_.size()) +
                                    ", LastExec=" + std::to_string(timeSinceLastExecution) + "s ago");
                }
            }
        }

        Logger::logInfo("[CommandExecutorQueue] Health monitor stopped");
    }

    size_t CommandExecutorQueue::getTotalCommandsAvailable() const {
        std::lock_guard<std::mutex> diskLock(diskMutex_);
        return commandQueue_.size() + pagingBuffer_.size() + diskQueue_.size();
    }

    void CommandExecutorQueue::loadFromAllSources() {
        size_t loadedFromBuffer = 0;
        size_t loadedFromDisk = 0;

        // Target to keep 1000 commands in RAM for smooth execution
        size_t targetLoad = 1000;
        size_t currentSize = commandQueue_.size();

        if (currentSize >= targetLoad) {
            return;  // Already have enough
        }

        size_t toLoad = targetLoad - currentSize;

        // Load from paging buffer first (already sorted)
        while (!pagingBuffer_.empty() && loadedFromBuffer < toLoad) {
            commandQueue_.push(pagingBuffer_.top());
            pagingBuffer_.pop();
            loadedFromBuffer++;
        }

        // If still need more, load from disk
        if (loadedFromBuffer < toLoad) {
            std::lock_guard<std::mutex> diskLock(diskMutex_);
            size_t diskToLoad = toLoad - loadedFromBuffer;

            while (!diskQueue_.empty() && loadedFromDisk < diskToLoad) {
                commandQueue_.push(diskQueue_.front());
                diskQueue_.pop_front();
                loadedFromDisk++;
            }
        }

        if (loadedFromBuffer > 0 || loadedFromDisk > 0) {
            Logger::logInfo("[CommandExecutorQueue] Loaded " +
                            std::to_string(loadedFromBuffer) + " from buffer, " +
                            std::to_string(loadedFromDisk) + " from disk. " +
                            "Queue now has " + std::to_string(commandQueue_.size()) + " commands");
        }
    }

    void CommandExecutorQueue::enqueue(const std::string &command, int priority, const std::string &jobId) {
        if (command.empty() || command.find_first_not_of(" \t\r\n") == std::string::npos) {
            return;
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

            commandQueue_.push(cmd);
            updateStats(false, false);

            if (commandQueue_.size() > MAX_COMMANDS_IN_RAM) {
                pageCommandsToDisk();
            }
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

        // Ensure processing is running
        if (!running_) {
            Logger::logWarning("[CommandExecutorQueue] Starting queue for file processing");
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

                    commandQueue_.push(cmd);
                    enqueuedCount++;

                    // Page periodically during bulk insert
                    if (commandQueue_.size() > MAX_COMMANDS_IN_RAM && enqueuedCount % 100 == 0) {
                        pageCommandsToDisk();
                    }
                }
            }

            updateStats(false, false);
        }

        Logger::logInfo("[CommandExecutorQueue] Enqueued " + std::to_string(enqueuedCount) +
                        " commands (priority=" + std::to_string(priority) + ", jobId=" + jobId + ")");

        // Wake processing
        queueCondition_.notify_all();
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

        static size_t logCounter = 0;
        bool shouldLog = (cmd.priority <= 3) || (++logCounter % 100 == 0);

        if (shouldLog) {
            Logger::logInfo("[CommandExecutorQueue] Executing: " + cmd.command +
                            " (priority=" + std::to_string(cmd.priority) + ")");
        }

        try {
            translator_->parseLine(cmd.command);
            updateStats(true, false);

        } catch (const GCodeTranslatorInvalidCommandException &e) {
            updateStats(false, true);
            Logger::logWarning("[CommandExecutorQueue] Invalid G-code: " + cmd.command);

        } catch (const GCodeTranslatorUnknownCommandException &e) {
            updateStats(false, true);
            Logger::logWarning("[CommandExecutorQueue] Unknown G-code: " + cmd.command);

        } catch (const std::exception &e) {
            updateStats(false, true);
            Logger::logError("[CommandExecutorQueue] Execution error for '" + cmd.command +
                             "': " + std::string(e.what()));
        }
    }

    void CommandExecutorQueue::pageCommandsToDisk() {
        if (commandQueue_.size() <= MAX_COMMANDS_IN_RAM / 2) {
            return;
        }

        size_t targetSize = MAX_COMMANDS_IN_RAM / 2;
        size_t movedToPagingBuffer = 0;

        while (commandQueue_.size() > targetSize && pagingBuffer_.size() < PAGING_BUFFER_SIZE) {
            pagingBuffer_.push(commandQueue_.top());
            commandQueue_.pop();
            movedToPagingBuffer++;
        }

        size_t flushedToDisk = 0;
        if (pagingBuffer_.size() >= PAGING_BUFFER_SIZE) {
            std::lock_guard<std::mutex> diskLock(diskMutex_);

            while (!pagingBuffer_.empty()) {
                PriorityCommand cmd = pagingBuffer_.top();
                pagingBuffer_.pop();
                diskQueue_.push_back(cmd);
                saveToDisk(cmd);
                flushedToDisk++;
            }

            stats_.diskPagedCommands += flushedToDisk;
        }

        if (movedToPagingBuffer > 0 || flushedToDisk > 0) {
            Logger::logInfo("[CommandExecutorQueue] Paging: moved " + std::to_string(movedToPagingBuffer) +
                            " to buffer, flushed " + std::to_string(flushedToDisk) + " to disk");
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

    CommandExecutorQueue::Statistics CommandExecutorQueue::getStatistics() const {
        std::lock_guard<std::mutex> lock(statsMutex_);
        Statistics result = stats_;
        result.currentQueueSize = getTotalCommandsAvailable();
        return result;
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