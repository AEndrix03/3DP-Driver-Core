#include "core/printer/job/PrintJobManager.hpp"
#include "logger/Logger.hpp"
#include <cmath>
#include <fstream>
#include <filesystem>

namespace core::print {
    PrintJobManager::PrintJobManager(std::shared_ptr<DriverInterface> driver,
                                     std::shared_ptr<CommandExecutorQueue> commandQueue)
        : driver_(driver), commandQueue_(commandQueue), currentState_(PrintJobState::Idle) {
    }

    bool PrintJobManager::startPrintJob(const std::string &gcodePath, const std::string &jobId) {
        std::lock_guard<std::mutex> lock(stateMutex_);

        if (currentState_ != PrintJobState::Idle) {
            Logger::logError("[PrintJobManager] Cannot start - job already active: " + currentJobId_);
            return false;
        }

        // Safety pre-checks
        updateState(PrintJobState::PreCheck);
        if (!isReadyToPrint()) {
            updateState(PrintJobState::Error);
            return false;
        }

        // Load file
        updateState(PrintJobState::Loading);

        // Count lines for progress tracking
        std::ifstream file(gcodePath);
        if (!file.is_open()) {
            Logger::logError("[PrintJobManager] Cannot open G-code file: " + gcodePath);
            updateState(PrintJobState::Error);
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

        // Initialize job
        currentJobId_ = jobId;
        currentFilePath_ = gcodePath;
        totalLines_ = lineCount;
        executedLines_ = 0;
        startTime_ = std::chrono::steady_clock::now();

        // Queue the entire G-code file
        commandQueue_->enqueueFile(gcodePath, 3, jobId); // Priority 3 for print jobs

        // Start heating if needed
        updateState(PrintJobState::Heating);
        // Temperature commands already sent via G-code

        updateState(PrintJobState::Printing);
        Logger::logInfo("[PrintJobManager] Print job started: " + jobId + " (" + std::to_string(lineCount) + " lines)");

        return true;
    }

    bool PrintJobManager::startPrintJobFromUrl(const std::string &gcodeUrl, const std::string &jobId) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (currentState_ != PrintJobState::Idle) {
            Logger::logError("[PrintJobManager] Cannot start download - job already active: " + currentJobId_);
            return false;
        }
        if (!downloader_) {
            downloader_ = std::make_unique<GCodeDownloader>();
        }
        currentJobId_ = jobId;
        updateState(PrintJobState::Loading);

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
        if (currentState_ != PrintJobState::Printing) {
            Logger::logWarning("[PrintJobManager] Cannot pause - not printing");
            return false;
        }

        try {
            driver_->system()->pause();
            updateState(PrintJobState::Paused);
            Logger::logInfo("[PrintJobManager] Job paused: " + currentJobId_);
            return true;
        } catch (const std::exception &e) {
            Logger::logError("[PrintJobManager] Pause failed: " + std::string(e.what()));
            return false;
        }
    }

    bool PrintJobManager::resumeJob() {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (currentState_ != PrintJobState::Paused) {
            Logger::logWarning("[PrintJobManager] Cannot resume - not paused");
            return false;
        }

        try {
            driver_->system()->resume();
            updateState(PrintJobState::Printing);
            Logger::logInfo("[PrintJobManager] Job resumed: " + currentJobId_);
            return true;
        } catch (const std::exception &e) {
            Logger::logError("[PrintJobManager] Resume failed: " + std::string(e.what()));
            return false;
        }
    }

    bool PrintJobManager::cancelJob() {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (currentState_ == PrintJobState::Idle) {
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

        // Emergency stop
        try {
            driver_->motion()->emergencyStop();
            updateState(PrintJobState::Cancelled);
            Logger::logInfo("[PrintJobManager] Job cancelled: " + currentJobId_);
            resetJob();
            return true;
        } catch (const std::exception &e) {
            Logger::logError("[PrintJobManager] Cancel failed: " + std::string(e.what()));
            updateState(PrintJobState::Error);
            return false;
        }
    }

    PrintJobState PrintJobManager::getCurrentState() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return currentState_;
    }

    bool PrintJobManager::isReadyToPrint() const {
        // Check homing
        if (!checkHoming()) {
            Logger::logError("[PrintJobManager] Printer not homed");
            return false;
        }

        // Check endstops
        if (!checkEndstops()) {
            Logger::logError("[PrintJobManager] Endstop error");
            return false;
        }

        // Check temperatures
        if (!checkTemperatures()) {
            Logger::logError("[PrintJobManager] Temperature check failed");
            return false;
        }

        // Check system ready
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

        if (totalLines_ > 0) {
            progress.percentComplete = (float(executedLines_) / totalLines_) * 100.0f;
        }

        auto now = std::chrono::steady_clock::now();
        progress.elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_);

        if (executedLines_ > 0 && progress.percentComplete > 0) {
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

    void PrintJobManager::updateState(PrintJobState newState) {
        if (currentState_ != newState) {
            PrintJobState oldState = currentState_;
            currentState_ = newState;
            Logger::logInfo(
                "[PrintJobManager] State change: " + stateToString(oldState) + " -> " + stateToString(newState));

            if (newState == PrintJobState::Error || newState == PrintJobState::Completed) {
                // Eventuali notifiche Kafka qui
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
        try {
            // Check if hotend and bed are at reasonable temperatures
            // This is a basic safety check - temperatures should be managed by G-code
            auto hotendResult = driver_->temperature()->getHotendTemperature();
            auto bedResult = driver_->temperature()->getBedTemperature();

            if (!hotendResult.isSuccess() || !bedResult.isSuccess()) {
                Logger::logWarning("[PrintJobManager] Could not read temperatures");
                return true; // Don't fail if we can't read temperatures
            }

            return true; // Temperature management handled by G-code
        } catch (const std::exception &e) {
            Logger::logError("[PrintJobManager] Temperature check failed: " + std::string(e.what()));
            return false;
        }
    }

    void PrintJobManager::resetJob() {
        currentJobId_.clear();
        currentFilePath_.clear();
        totalLines_ = 0;
        executedLines_ = 0;
        startTime_ = std::chrono::steady_clock::now();
    }

    std::string PrintJobManager::stateToString(PrintJobState state) const {
        switch (state) {
            case PrintJobState::Idle: return "Idle";
            case PrintJobState::Loading: return "Loading";
            case PrintJobState::PreCheck: return "PreCheck";
            case PrintJobState::Heating: return "Heating";
            case PrintJobState::Ready: return "Ready";
            case PrintJobState::Printing: return "Printing";
            case PrintJobState::Paused: return "Paused";
            case PrintJobState::Finishing: return "Finishing";
            case PrintJobState::Completed: return "Completed";
            case PrintJobState::Error: return "Error";
            case PrintJobState::Cancelled: return "Cancelled";
            default: return "Unknown";
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
            updateState(PrintJobState::Error);
            resetJob();
            return;
        }

        Logger::logInfo("[PrintJobManager] Download completed, starting print job with: " + filePath);

        if (startPrintJob(filePath, currentJobId_)) {
            Logger::logInfo("[PrintJobManager] Print job started successfully from downloaded G-code");
        } else {
            Logger::logError("[PrintJobManager] Failed to start print job from downloaded G-code");
            std::filesystem::remove(filePath);
        }
    }
} // namespace core::print
