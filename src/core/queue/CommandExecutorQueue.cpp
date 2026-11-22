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
            : translator_(std::move(translator)), running_(false), stopping_(false),
              lastExecutionTime_(std::chrono::steady_clock::now()) {
        if (!translator_) {
            throw std::invalid_argument("GCodeTranslator cannot be null");
        }
    }

    CommandExecutorQueue::~CommandExecutorQueue() {
        stop();
    }

    void CommandExecutorQueue::start() {
        std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
        if (running_) {
            return;
        }

        // Un thread terminato per eccezione resta joinable: riassegnarlo chiamerebbe std::terminate
        if (processingThread_.joinable()) processingThread_.join();
        if (healthThread_.joinable()) healthThread_.join();

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
        std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
        if (!running_ && !processingThread_.joinable() && !healthThread_.joinable()) return;

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
        auto lastLogTime = std::chrono::steady_clock::now();

        try {
            while (running_) {
                processingThreadAlive_ = true; // Internal Heartbeat

                PriorityCommand command;
                bool hasCommand = false;

                // FIXED: Use consistent mutex ordering
                {
                    std::unique_lock<std::mutex> lock(queueMutex_);

                    // Check if stopping
                    if (stopping_) break;

                    std::string brokenJob;
                    if (!takeNext(command, brokenJob)) {
                        if (brokenJob.empty()) {
                            // coda vuota, oppure in pausa con soli comandi di job in attesa
                            queueCondition_.wait_for(lock, std::chrono::milliseconds(500));
                            continue;
                        }
                        lock.unlock();
                        haltAfterFault(brokenJob, "g-code file ended before the expected number of lines");
                        continue;
                    }
                    hasCommand = true;
                }

                if (hasCommand) {
                    auto driver = translator_->getDriver();
                    const uint64_t faultsBefore = driver ? driver->faultEpoch() : 0;

                    beginLine(command);
                    executeCommand(command);
                    executedCount++;
                    lastExecutionTime_ = std::chrono::steady_clock::now();

                    const bool faulted = driver && driver->faultEpoch() != faultsBefore;
                    endLine(command, faulted);
                    persistCheckpoint(faulted); // al guasto subito, altrimenti al massimo ogni 2 s

                    if (faulted) {
                        // Timeout, desync, riavvio firmware, macchina in errore o comando
                        // di job fallito: non si prosegue alla cieca.
                        haltAfterFault(command.jobId, driver->lastFaultReason());
                    } else if (markCommandDone(command.jobId)) {
                        std::function<void(const std::string &)> callback;
                        {
                            std::lock_guard<std::mutex> cb(callbackMutex_);
                            callback = jobFinishedCallback_;
                        }
                        if (callback) callback(command.jobId);
                    }

                    if (executedCount % 1000 == 0) {
                        auto now = std::chrono::steady_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastLogTime).count();
                        Logger::logInfo("[CommandExecutorQueue] Executed: " + std::to_string(executedCount) +
                                        ", Rate: " + std::to_string(elapsed > 0 ? 1000 / elapsed : 0) + " cmd/s");
                        lastLogTime = now;
                    }
                }
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

            if (totalCommands == 0) {
                lastExecutionTime_ = std::chrono::steady_clock::now();
                continue;
            }

            // Check if processing thread is alive
            bool threadAlive = processingThreadAlive_.load();

            // Un comando lungo (homing, M109, Z lento) non e' uno stallo: il driver ha
            // i propri timeout verso il firmware. Qui si segnala soltanto.
            const bool isStalled = !paused_ && (timeSinceLastExecution > 120 || !threadAlive);

            if (isStalled && !executionStalled_) {
                executionStalled_ = true;
                Logger::logWarning("[CommandExecutorQueue] No command completed for " +
                                   std::to_string(timeSinceLastExecution) + "s, thread alive: " +
                                   (threadAlive ? "true" : "false"));
                queueCondition_.notify_all();
            } else if (!isStalled) {
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
        // Skip comments and empty lines
        if (cmd.command.empty() || cmd.command[0] == ';' || cmd.command[0] == '%') {
            updateStats(true, false);
            return;
        }

        if (cmd.priority <= 2) {
            Logger::logInfo("[CommandExecutorQueue] EXECUTING: " + cmd.command +
                            " (priority=" + std::to_string(cmd.priority) + ", jobId=" + cmd.jobId + ")");
        }

        try {
            translator_->parseLine(cmd.command);
            updateStats(true, false);
        } catch (const GCodeTranslatorInvalidCommandException &e) {
            updateStats(false, true);
            Logger::logWarning("[CommandExecutorQueue] Invalid G-code: " + cmd.command + " - " + std::string(e.what()));
        } catch (const GCodeTranslatorUnknownCommandException &e) {
            updateStats(false, true);
            Logger::logWarning("[CommandExecutorQueue] Unknown G-code: " + cmd.command + " - " + std::string(e.what()));
        } catch (const std::exception &e) {
            // Il comando e' valido ma la macchina non l'ha eseguito (movimento rifiutato,
            // riscaldamento fallito, ...): continuare produrrebbe una stampa sbagliata.
            updateStats(false, true);
            const std::string reason = "'" + cmd.command + "' failed: " + std::string(e.what());
            Logger::logError("[CommandExecutorQueue] " + reason);
            if (auto driver = translator_->getDriver()) {
                driver->raiseFault(reason);
            }
        }

        // Il progresso conta i comandi effettivamente elaborati
        jobs::JobTracker::getInstance().updateJobProgress(cmd.jobId, cmd.command);
    }

    void CommandExecutorQueue::haltAfterFault(const std::string &jobId, const std::string &reason) {
        size_t dropped = 0;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            dropped = commandQueue_.size();
            for (const auto &source: files_) dropped += source->remaining;
            commandQueue_ = std::priority_queue<PriorityCommand>();
            files_.clear();
            pendingByJob_.clear();
        }

        Logger::logError("[CommandExecutorQueue] HALTED after fault: " + reason + " - dropped " +
                         std::to_string(dropped) + " pending commands");

        if (!jobId.empty()) {
            auto &tracker = jobs::JobTracker::getInstance();
            const auto info = tracker.getJobInfo(jobId);
            // Un job gia' annullato dall'utente resta "annullato"
            if (info && (info->state == print::JobState::RUNNING || info->state == print::JobState::PAUSED)) {
                tracker.failJob(jobId, reason);
            }
        }

        std::function<void(const std::string &, const std::string &)> callback;
        {
            std::lock_guard<std::mutex> cb(callbackMutex_);
            callback = jobFaultCallback_;
        }
        if (callback) callback(jobId, reason);
    }

    bool CommandExecutorQueue::markCommandDone(const std::string &jobId) {
        if (jobId.empty()) return false;

        std::lock_guard<std::mutex> lock(queueMutex_);
        auto it = pendingByJob_.find(jobId);
        if (it == pendingByJob_.end()) return false;

        if (it->second > 1) {
            it->second--;
            return false;
        }
        pendingByJob_.erase(it);
        return true;
    }

    void CommandExecutorQueue::setPaused(bool paused) {
        paused_ = paused;
        Logger::logInfo(std::string("[CommandExecutorQueue] ") + (paused ? "Paused" : "Resumed"));
        queueCondition_.notify_all();
    }

    void CommandExecutorQueue::setJobFaultCallback(
            std::function<void(const std::string &, const std::string &)> callback) {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        jobFaultCallback_ = std::move(callback);
    }

    void CommandExecutorQueue::setJobFinishedCallback(std::function<void(const std::string &)> callback) {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        jobFinishedCallback_ = std::move(callback);
    }

    size_t CommandExecutorQueue::getTotalCommandsAvailable() const {
        std::lock_guard<std::mutex> lock(queueMutex_);
        size_t total = commandQueue_.size();
        for (const auto &source: files_) total += source->remaining;
        return total;
    }

    bool CommandExecutorQueue::isSkippable(const std::string &line) {
        return line.empty() || line.find_first_not_of(" \t") == std::string::npos ||
               line[0] == ';' || line[0] == '%';
    }

    bool CommandExecutorQueue::nextFromFile(FileSource &source, PriorityCommand &cmd) {
        cmd.priority = source.priority;
        cmd.jobId = source.jobId;
        cmd.fileLine = -1;

        if (!source.preamble.empty()) {
            cmd.command = std::move(source.preamble.front());
            source.preamble.pop_front();
            return true;
        }

        std::string line;
        while (std::getline(source.file, line)) {
            const size_t index = source.nextIndex++;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (isSkippable(line)) continue;
            cmd.command = std::move(line);
            cmd.fileLine = static_cast<int64_t>(index);
            return true;
        }

        if (!source.ending.empty()) {
            cmd.command = std::move(source.ending.front());
            source.ending.pop_front();
            return true;
        }
        return false;
    }

    bool CommandExecutorQueue::takeNext(PriorityCommand &cmd, std::string &brokenJob) {
        FileSource *source = files_.empty() ? nullptr : files_.front().get();
        const bool hasSingle = !commandQueue_.empty();
        if (!source && !hasSingle) return false;

        // Tra un comando singolo e il job vince la priorita' piu' alta, poi chi e' arrivato prima
        const bool useSingle = hasSingle &&
                               (!source || commandQueue_.top().priority < source->priority ||
                                (commandQueue_.top().priority == source->priority &&
                                 commandQueue_.top().sequenceId < source->sequenceId));

        // In pausa passano solo i comandi urgenti (priorita' < 3)
        const int priority = useSingle ? commandQueue_.top().priority : source->priority;
        if (paused_ && priority >= 3) return false;

        if (useSingle) {
            cmd = commandQueue_.top();
            commandQueue_.pop();
            return true;
        }

        if (!nextFromFile(*source, cmd)) {
            brokenJob = source->jobId;
            files_.pop_front();
            return false;
        }
        cmd.sequenceId = nextSequenceId_.fetch_add(1);
        if (--source->remaining == 0) files_.pop_front();
        return true;
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

            if (!jobId.empty()) pendingByJob_[jobId]++;

            commandQueue_.push(cmd);
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

                    commandQueue_.push(cmd);
                    enqueuedCount++;
                }
            }
            if (!jobId.empty() && enqueuedCount > 0) pendingByJob_[jobId] += enqueuedCount;
            stats_.totalEnqueued += enqueuedCount;
        }

        Logger::logInfo("[CommandExecutorQueue] Successfully enqueued " + std::to_string(enqueuedCount) + " commands");

        queueCondition_.notify_all();
    }

    void CommandExecutorQueue::enqueueFile(const std::string &filePath, int priority, const std::string &jobId,
                                           size_t startLine, const std::vector<std::string> &preamble,
                                           const std::string &endGCode) {
        auto source = std::make_unique<FileSource>();
        source->jobId = jobId;
        source->priority = priority;

        // Primo passaggio: si contano i comandi (servono per l'avanzamento e per sapere
        // quando il job e' finito) senza tenerli in memoria.
        size_t fileCommands = 0;
        {
            std::ifstream counter(filePath);
            if (!counter.is_open()) {
                Logger::logError("[CommandExecutorQueue] Cannot open file: " + filePath);
                return;
            }
            std::string line;
            size_t index = 0;
            while (std::getline(counter, line)) {
                if (index++ < startLine) continue;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!isSkippable(line)) ++fileCommands;
            }
        }

        for (const auto &text: preamble) {
            if (!isSkippable(text)) source->preamble.push_back(text);
        }

        // G-code di fine stampa: in coda al job, dopo l'ultima riga del file
        std::istringstream endStream(endGCode);
        std::string line;
        while (std::getline(endStream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!isSkippable(line)) source->ending.push_back(line);
        }

        source->remaining = source->preamble.size() + fileCommands + source->ending.size();
        if (source->remaining == 0) {
            Logger::logWarning("[CommandExecutorQueue] No valid commands in file");
            return;
        }

        // Secondo passaggio, pigro: il file resta aperto e posizionato sulla riga di partenza
        source->file.open(filePath);
        for (size_t skipped = 0; skipped < startLine && std::getline(source->file, line); ++skipped) {}
        source->nextIndex = startLine;

        Logger::logInfo("[CommandExecutorQueue] File queued: " + std::to_string(source->remaining) +
                        " commands (from line " + std::to_string(startLine) + ")");

        core::jobs::JobTracker::getInstance().startJob(jobId, source->remaining);
        if (startLine == 0) {
            core::state::StateTracker::getInstance().resetForNewJob();
        }

        {
            std::lock_guard<std::mutex> lock(checkpointMutex_);
            trackedJobId_ = jobId;
            trackedPath_ = filePath;
            trackedEndGCode_ = endGCode;
            inFlight_.clear();
            pendingCheckpoint_.reset();
        }

        if (!running_) start();

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            source->sequenceId = nextSequenceId_.fetch_add(1);
            if (!jobId.empty()) pendingByJob_[jobId] += source->remaining;
            stats_.totalEnqueued += source->remaining;
            files_.push_back(std::move(source));
        }
        queueCondition_.notify_all();
    }

    // ------------------------------------------------------------------
    // Checkpoint
    // ------------------------------------------------------------------

    print::JobCheckpoint CommandExecutorQueue::captureState(int64_t fileLine) const {
        auto &state = core::state::StateTracker::getInstance();
        const auto motion = state.getMotionSnapshot();

        print::JobCheckpoint c;
        c.jobId = trackedJobId_;
        c.gcodePath = trackedPath_;
        c.endGCode = trackedEndGCode_;
        c.nextLine = static_cast<size_t>(fileLine);
        c.positionKnown = motion.positionKnown;
        c.x = motion.x;
        c.y = motion.y;
        c.z = motion.z;
        c.absoluteXYZ = motion.absoluteXYZ;
        c.absoluteE = motion.absoluteE;
        c.e = state.getCurrentEPosition();
        c.feedrate = state.getCurrentFeedRate();
        c.hotendTarget = state.getHotendTargetTemp();
        c.fanSpeed = state.getCurrentFanSpeed();
        c.layer = state.getCurrentLayer();
        return c;
    }

    void CommandExecutorQueue::beginLine(const PriorityCommand &cmd) {
        std::lock_guard<std::mutex> lock(checkpointMutex_);
        if (cmd.fileLine < 0 || cmd.jobId != trackedJobId_) return;
        auto driver = translator_->getDriver();
        inFlight_.push_back({captureState(cmd.fileLine), 0, driver ? driver->sequenceEpoch() : 0, false});
    }

    void CommandExecutorQueue::endLine(const PriorityCommand &cmd, bool faulted) {
        std::lock_guard<std::mutex> lock(checkpointMutex_);
        if (cmd.fileLine < 0 || cmd.jobId != trackedJobId_ || inFlight_.empty()) return;
        auto driver = translator_->getDriver();
        if (!driver) return;

        if (!faulted) {
            inFlight_.back().lastCommand = driver->lastIssuedCommand();
            inFlight_.back().issued = true;
        }

        // Una riga e' completata quando il firmware ha ESEGUITO il suo ultimo comando
        // (campo D della risposta), non quando lo ha solo accodato. Vale anche dopo un
        // guasto: la riga interrotta resta (non e' "issued"), quelle prima vengono scartate.
        const uint32_t completed = driver->lastCompletedCommand();
        const uint32_t epoch = driver->sequenceEpoch();
        while (!inFlight_.empty() && inFlight_.front().issued &&
               inFlight_.front().sequenceEpoch == epoch && inFlight_.front().lastCommand <= completed) {
            inFlight_.pop_front();
        }

        // Punto di ripresa: la prima riga non ancora completata, altrimenti la successiva
        pendingCheckpoint_ = inFlight_.empty() ? captureState(cmd.fileLine + 1) : inFlight_.front().start;
    }

    void CommandExecutorQueue::persistCheckpoint(bool force) {
        std::optional<print::JobCheckpoint> checkpoint;
        {
            std::lock_guard<std::mutex> lock(checkpointMutex_);
            if (!pendingCheckpoint_) return;
            const auto now = std::chrono::steady_clock::now();
            if (!force && now - lastCheckpointSave_ < std::chrono::seconds(2)) return;
            lastCheckpointSave_ = now;
            checkpoint = pendingCheckpoint_;
        }
        checkpoint->save();
    }

    void CommandExecutorQueue::saveCheckpointNow() {
        persistCheckpoint(true);
    }

    void CommandExecutorQueue::forgetJob(const std::string &jobId) {
        {
            std::lock_guard<std::mutex> lock(checkpointMutex_);
            if (trackedJobId_ == jobId) {
                trackedJobId_.clear();
                inFlight_.clear();
                pendingCheckpoint_.reset();
            }
        }
        print::JobCheckpoint::remove(jobId);
    }

    size_t CommandExecutorQueue::getQueueSize() const {
        return getTotalCommandsAvailable();
    }

    void CommandExecutorQueue::clearQueue() {
        std::lock_guard<std::mutex> lock(queueMutex_);

        size_t clearedCount = commandQueue_.size();
        for (const auto &source: files_) clearedCount += source->remaining;

        std::priority_queue<PriorityCommand> emptyQueue;
        commandQueue_.swap(emptyQueue);
        files_.clear();
        pendingByJob_.clear();

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
