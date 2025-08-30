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
            : translator_(std::move(translator)), running_(false), stopping_(false) {
        if (!translator_) {
            throw std::invalid_argument("GCodeTranslator cannot be null");
        }

        initDiskFile();
        Logger::logInfo("[CommandExecutorQueue] Initialized with enhanced paging (10k RAM + 5k buffer)");
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

        Logger::logInfo("[CommandExecutorQueue] Starting with initial queue sizes:");
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            Logger::logInfo("  RAM Queue: " + std::to_string(commandQueue_.size()));
            Logger::logInfo("  Paging Buffer: " + std::to_string(pagingBuffer_.size()));
        }
        {
            std::lock_guard<std::mutex> diskLock(diskMutex_);
            Logger::logInfo("  Disk Queue: " + std::to_string(diskQueue_.size()));
        }

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

        clearQueue();
        Logger::logInfo("[CommandExecutorQueue] Stopped");
    }

    bool CommandExecutorQueue::isRunning() const {
        return running_;
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

        // Always notify after enqueue to wake processing thread
        queueCondition_.notify_one();
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

        enqueueCommands(commands, priority, jobId);

        // Force wake the processing thread after bulk enqueue
        queueCondition_.notify_all();
    }

    void CommandExecutorQueue::enqueueCommands(const std::vector<std::string> &commands, int priority,
                                               const std::string &jobId) {
        if (commands.empty()) {
            Logger::logWarning("[CommandExecutorQueue] Empty command vector ignored");
            return;
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

                    commandQueue_.push(cmd);
                    enqueuedCount++;

                    // Page to disk if needed during bulk insert
                    if (commandQueue_.size() > MAX_COMMANDS_IN_RAM && enqueuedCount % 100 == 0) {
                        pageCommandsToDisk();
                    }
                }
            }

            updateStats(false, false);
        }

        Logger::logInfo("[CommandExecutorQueue] Enqueued " + std::to_string(enqueuedCount) +
                        " commands (priority=" + std::to_string(priority) + ", jobId=" + jobId + ")");

        // Wake processing thread
        queueCondition_.notify_all();
    }

    size_t CommandExecutorQueue::getQueueSize() const {
        std::lock_guard<std::mutex> lock(queueMutex_);
        std::lock_guard<std::mutex> diskLock(diskMutex_);
        return commandQueue_.size() + pagingBuffer_.size() + diskQueue_.size();
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

    CommandExecutorQueue::Statistics CommandExecutorQueue::getStatistics() const {
        std::lock_guard<std::mutex> lock(statsMutex_);
        std::lock_guard<std::mutex> queueLock(queueMutex_);
        std::lock_guard<std::mutex> diskLock(diskMutex_);

        Statistics result = stats_;
        result.currentQueueSize = commandQueue_.size() + pagingBuffer_.size() + diskQueue_.size();

        return result;
    }

    void CommandExecutorQueue::processingLoop() {
        Logger::logInfo("[CommandExecutorQueue] Processing loop started");

        size_t executedCount = 0;
        auto lastLogTime = std::chrono::steady_clock::now();
        int emptyLoopCount = 0;

        while (running_) {
            PriorityCommand command;
            bool hasCommand = false;

            {
                std::unique_lock<std::mutex> lock(queueMutex_);

                // Load from other sources if main queue is empty
                if (commandQueue_.empty()) {
                    loadFromAllSources();
                }

                // Check if we have commands after loading
                bool hasAnyCommands = !commandQueue_.empty();
                if (!hasAnyCommands) {
                    std::lock_guard<std::mutex> diskLock(diskMutex_);
                    hasAnyCommands = !diskQueue_.empty() || !pagingBuffer_.empty();
                }

                // Only wait if we truly have no commands anywhere
                if (!hasAnyCommands) {
                    queueCondition_.wait_for(lock, std::chrono::milliseconds(100), [this] {
                        return !commandQueue_.empty() || stopping_;
                    });

                    // Try loading again after wait
                    if (commandQueue_.empty()) {
                        loadFromAllSources();
                    }
                }

                if (stopping_ && commandQueue_.empty()) {
                    std::lock_guard<std::mutex> diskLock(diskMutex_);
                    if (diskQueue_.empty() && pagingBuffer_.empty()) {
                        Logger::logInfo("[CommandExecutorQueue] All queues empty, stopping");
                        break;
                    }
                }

                if (!commandQueue_.empty()) {
                    command = commandQueue_.top();
                    commandQueue_.pop();
                    hasCommand = true;
                    emptyLoopCount = 0;
                } else {
                    emptyLoopCount++;
                    if (emptyLoopCount % 100 == 0) {
                        Logger::logWarning("[CommandExecutorQueue] Queue empty for " +
                                           std::to_string(emptyLoopCount) + " iterations");
                    }
                }
            }

            if (hasCommand) {
                executeCommand(command);
                executedCount++;

                // Log progress
                if (executedCount % 100 == 0) {
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastLogTime).count();

                    Logger::logInfo("[CommandExecutorQueue] Progress: " + std::to_string(executedCount) +
                                    " executed, queue: " + std::to_string(getQueueSize()) +
                                    ", rate: " + std::to_string(elapsed > 0 ? 100 / elapsed : 0) + " cmd/s");
                    lastLogTime = now;
                }
            }
        }

        Logger::logInfo("[CommandExecutorQueue] Processing loop finished. Total executed: " +
                        std::to_string(executedCount));
    }

    void CommandExecutorQueue::loadFromAllSources() {
        // Load from paging buffer first
        if (!pagingBuffer_.empty()) {
            size_t loadCount = std::min(size_t(1000), pagingBuffer_.size());
            for (size_t i = 0; i < loadCount && !pagingBuffer_.empty(); ++i) {
                commandQueue_.push(pagingBuffer_.top());
                pagingBuffer_.pop();
            }
            Logger::logInfo("[CommandExecutorQueue] Loaded " + std::to_string(loadCount) +
                            " commands from paging buffer");
        }

        // Then load from disk if still empty
        if (commandQueue_.empty() && !diskQueue_.empty()) {
            std::lock_guard<std::mutex> diskLock(diskMutex_);
            size_t loadCount = std::min(size_t(1000), diskQueue_.size());
            for (size_t i = 0; i < loadCount && !diskQueue_.empty(); ++i) {
                commandQueue_.push(diskQueue_.front());
                diskQueue_.pop_front();
            }
            Logger::logInfo("[CommandExecutorQueue] Loaded " + std::to_string(loadCount) +
                            " commands from disk");
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

            static int consecutiveErrors = 0;
            consecutiveErrors++;

            if (consecutiveErrors > 5) {
                Logger::logWarning("[CommandExecutorQueue] Multiple consecutive errors, pausing briefly");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } else if (consecutiveErrors > 20) {
                Logger::logError("[CommandExecutorQueue] Too many errors, longer pause");
                std::this_thread::sleep_for(std::chrono::seconds(1));
                consecutiveErrors = 0;
            }
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