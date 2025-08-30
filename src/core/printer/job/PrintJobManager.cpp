#include "core/printer/job/PrintJobManager.hpp"
#include "core/printer/job/tracking/JobTracker.hpp"
#include "logger/Logger.hpp"
#include <cmath>
#include <fstream>
#include <filesystem>

namespace core::print {
    PrintJobManager::PrintJobManager(std::shared_ptr<DriverInterface> driver,
                                     std::shared_ptr<CommandExecutorQueue> commandQueue)
            : driver_(driver), commandQueue_(commandQueue), currentState_(JobState::CREATED) {
    }

    bool PrintJobManager::startPrintJob(const std::string &gcodePath, const std::string &jobId) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return startPrintJobInternal(gcodePath, jobId);
    }

    bool PrintJobManager::startPrintJobInternal(const std::string &gcodePath, const std::string &jobId) {
        if (currentState_ == JobState::RUNNING) {
            Logger::logError("[PrintJobManager] Cannot start - job already active: " + currentJobId_);
            return false;
        }

        // Safety pre-checks
        updateState(JobState::PRECHECK);
        if (!isReadyToPrint()) {
            updateState(JobState::FAILED);
            return false;
        }

        // Send system start command
        driver_->system()->startPrint();

        // Update driver state
        driver_->setState(PrintState::Printing);

        // Load file
        updateState(JobState::LOADING);

        // Count lines for progress tracking
        std::ifstream file(gcodePath);
        if (!file.is_open()) {
            Logger::logError("[PrintJobManager] Cannot open G-code file: " + gcodePath);
            updateState(JobState::FAILED);
            driver_->setState(PrintState::Error);
            return false;
        }

        std::string line;
        size_t lineCount = 0;
        while (std::getline(file, line)) {
            if (!line.empty() && line[0] != ';' && line[0] != '%') {
                lineCount++;
            }
        }
        file.close();

        // Initialize job in tracker
        auto &jobTracker = jobs::JobTracker::getInstance();
        jobTracker.startJob(jobId, lineCount);

        // Initialize job manager state
        currentJobId_ = jobId;
        currentFilePath_ = gcodePath;
        totalLines_ = lineCount;
        executedLines_ = 0;
        startTime_ = std::chrono::steady_clock::now();

        // Ensure command queue is running
        if (commandQueue_ && !commandQueue_->isRunning()) {
            Logger::logInfo("[PrintJobManager] Starting command executor queue");
            commandQueue_->start();
        }

        // Queue the entire G-code file
        Logger::logInfo("[PrintJobManager] Enqueuing G-code file with " + std::to_string(lineCount) + " commands");
        commandQueue_->enqueueFile(gcodePath, 3, jobId);

        // Update states
        updateState(JobState::RUNNING);

        Logger::logInfo("[PrintJobManager] Print job started: " + jobId + " (" + std::to_string(lineCount) + " lines)");
        return true;
    }

    bool PrintJobManager::startPrintJobFromUrl(const std::string &gcodeUrl, const std::string &jobId) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (currentState_ == JobState::RUNNING) {
            Logger::logError("[PrintJobManager] Cannot start download - job already active: " + currentJobId_);
            return false;
        }

        if (!downloader_) {
            downloader_ = std::make_unique<GCodeDownloader>();
        }

        currentJobId_ = jobId;
        updateState(JobState::LOADING);

        downloader_->downloadAsync(
                gcodeUrl, jobId,
                [this](const DownloadProgress &progress) { onDownloadProgress(progress); },
                [this](bool success, const std::string &filePath, const std::string &error) {
                    onDownloadCompleted(success, filePath, error);
                }
        );

        Logger::logInfo("[PrintJobManager] Started G-code download for job: " + jobId);
        return true;
    }

    bool PrintJobManager::pauseJob() {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (currentState_ != JobState::RUNNING) {
            Logger::logWarning("[PrintJobManager] Cannot pause - not printing");
            return false;
        }

        try {
            driver_->system()->pause();
            driver_->setState(PrintState::Paused);
            updateState(JobState::PAUSED);

            // Update job tracker
            auto &jobTracker = jobs::JobTracker::getInstance();
            jobTracker.pauseJob(currentJobId_);

            Logger::logInfo("[PrintJobManager] Job paused: " + currentJobId_);
            return true;
        } catch (const std::exception &e) {
            Logger::logError("[PrintJobManager] Pause failed: " + std::string(e.what()));
            return false;
        }
    }

    bool PrintJobManager::resumeJob() {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (currentState_ != JobState::PAUSED) {
            Logger::logWarning("[PrintJobManager] Cannot resume - not paused");
            return false;
        }

        try {
            driver_->system()->resume();
            driver_->setState(PrintState::Printing);
            updateState(JobState::RUNNING);

            // Update job tracker
            auto &jobTracker = jobs::JobTracker::getInstance();
            jobTracker.resumeJob(currentJobId_);

            Logger::logInfo("[PrintJobManager] Job resumed: " + currentJobId_);
            return true;
        } catch (const std::exception &e) {
            Logger::logError("[PrintJobManager] Resume failed: " + std::string(e.what()));
            return false;
        }
    }

    bool PrintJobManager::cancelJob() {
        std::lock_guard<std::mutex> lock(stateMutex_);

        if (!(currentState_ == JobState::RUNNING || currentState_ == JobState::PAUSED ||
              currentState_ == JobState::LOADING || currentState_ == JobState::PRECHECK)) {
            Logger::logWarning("[PrintJobManager] No job to cancel");
            return false;
        }

        // Cancel download if in progress
        if (downloader_ && downloader_->isDownloading()) {
            downloader_->cancelDownload();
        }

        // Clear command queue
        if (commandQueue_) {
            commandQueue_->clearQueue();
        }

        // Update job tracker
        auto &jobTracker = jobs::JobTracker::getInstance();
        jobTracker.cancelJob(currentJobId_);

        // Emergency stop
        try {
            driver_->motion()->emergencyStop();
            driver_->setState(PrintState::Idle);
            updateState(JobState::CANCELLED);

            Logger::logInfo("[PrintJobManager] Job cancelled: " + currentJobId_);
            resetJob();
            return true;
        } catch (const std::exception &e) {
            Logger::logError("[PrintJobManager] Cancel failed: " + std::string(e.what()));
            updateState(JobState::FAILED);
            driver_->setState(PrintState::Error);
            return false;
        }
    }

    JobState PrintJobManager::getCurrentState() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return currentState_;
    }

    bool PrintJobManager::isReadyToPrint() const {
        // Basic safety check - removed strict requirements
        PrintState driverState = driver_->getState();
        if (driverState == PrintState::Error) {
            Logger::logError("[PrintJobManager] Driver in error state");
            return false;
        }
        return true;
    }

    PrintJobProgress PrintJobManager::getProgress() const {
        std::lock_guard<std::mutex> lock(stateMutex_);

        PrintJobProgress progress;
        progress.jobId = currentJobId_;
        progress.state = currentState_;
        progress.linesExecuted = executedLines_;
        progress.totalLines = totalLines_;

        // Get progress from job tracker for accurate count
        auto &jobTracker = jobs::JobTracker::getInstance();
        auto jobInfo = jobTracker.getJobInfo(currentJobId_);
        if (jobInfo.has_value()) {
            progress.linesExecuted = jobInfo->executedCommands;
            progress.percentComplete = jobInfo->getProgress();
        } else if (totalLines_ > 0) {
            progress.percentComplete = (float(executedLines_) / totalLines_) * 100.0f;
        }

        auto now = std::chrono::steady_clock::now();
        progress.elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_);

        if (progress.percentComplete > 0) {
            auto totalTime = progress.elapsed.count() / (progress.percentComplete / 100.0f);
            progress.estimated = std::chrono::seconds(static_cast<long>(totalTime));
        }

        // Get current position
        auto pos = driver_->motion()->getPosition();
        if (pos) {
            progress.currentPosition = *pos;
        }

        return progress;
    }

    void PrintJobManager::updateState(JobState newState) {
        if (currentState_ != newState) {
            JobState oldState = currentState_;
            currentState_ = newState;

            Logger::logInfo(
                    "[PrintJobManager] State change: " + stateToString(oldState) + " -> " + stateToString(newState));

            // Update job tracker state
            auto &jobTracker = jobs::JobTracker::getInstance();
            if (newState == JobState::FAILED) {
                jobTracker.failJob(currentJobId_, "Job failed");
            } else if (newState == JobState::COMPLETED) {
                jobTracker.completeJob(currentJobId_);
            }
        }
    }

    bool PrintJobManager::checkHoming() const {
        try {
            auto position = driver_->motion()->getPosition();
            if (!position) {
                Logger::logWarning("[PrintJobManager] Cannot get current position");
                return false;
            }
            return std::isfinite(position->x) && std::isfinite(position->y) && std::isfinite(position->z);
        } catch (const std::exception &e) {
            Logger::logError("[PrintJobManager] Homing check failed: " + std::string(e.what()));
            return false;
        }
    }

    bool PrintJobManager::checkEndstops() const {
        try {
            auto result = driver_->endstop()->readEndstopStatus();
            if (!result.isSuccess()) {
                Logger::logError("[PrintJobManager] Endstop check failed: " + result.message);
                return false;
            }

            for (const auto &line: result.body) {
                if (line.find("TRIGGERED") != std::string::npos) {
                    Logger::logWarning("[PrintJobManager] Endstop triggered: " + line);
                    return false;
                }
            }
            return true;
        } catch (const std::exception &e) {
            Logger::logError("[PrintJobManager] Endstop check exception: " + std::string(e.what()));
            return false;
        }
    }

    bool PrintJobManager::checkTemperatures() const {
        // Temperature checks disabled - managed by G-code
        return true;
    }

    void PrintJobManager::resetJob() {
        currentJobId_.clear();
        currentFilePath_.clear();
        totalLines_ = 0;
        executedLines_ = 0;
        startTime_ = std::chrono::steady_clock::now();
    }

    std::string PrintJobManager::stateToString(JobState state) const {
        switch (state) {
            case JobState::CREATED:
                return "Created";
            case JobState::QUEUED:
                return "Queued";
            case JobState::LOADING:
                return "Loading";
            case JobState::PRECHECK:
                return "PreCheck";
            case JobState::HEATING:
                return "Heating";
            case JobState::HOMING:
                return "Homing";
            case JobState::RUNNING:
                return "Running";
            case JobState::PAUSED:
                return "Paused";
            case JobState::COMPLETED:
                return "Completed";
            case JobState::FAILED:
                return "Failed";
            case JobState::CANCELLED:
                return "Cancelled";
            default:
                return "Unknown";
        }
    }

    void PrintJobManager::onDownloadProgress(const DownloadProgress &progress) {
        Logger::logInfo("[PrintJobManager] Download progress: " + std::to_string(int(progress.percentage)) + "% (" +
                        std::to_string(progress.downloadedBytes / 1024) + " KB)");
    }

    void PrintJobManager::onDownloadCompleted(bool success, const std::string &filePath, const std::string &error) {
        std::lock_guard<std::mutex> lock(stateMutex_);

        if (!success) {
            Logger::logError("[PrintJobManager] Download failed: " + error);
            updateState(JobState::FAILED);
            driver_->setState(PrintState::Error);
            resetJob();
            return;
        }

        Logger::logInfo("[PrintJobManager] Download completed, starting print job with: " + filePath);

        if (startPrintJobInternal(filePath, currentJobId_)) {
            Logger::logInfo("[PrintJobManager] Print job started successfully from downloaded G-code");
        } else {
            Logger::logError("[PrintJobManager] Failed to start print job from downloaded G-code");
            std::filesystem::remove(filePath);
        }
    }
} // namespace core::print