#include "core/printer/job/PrintJobManager.hpp"
#include "core/printer/job/tracking/JobTracker.hpp"
#include "logger/Logger.hpp"
#include <cmath>
#include <fstream>
#include <filesystem>
#include "core/printer/job/JobCheckpoint.hpp"
#include "core/utils/FloatFormatter.hpp"

namespace core::print {
    namespace {
        constexpr double PARK_LIFT_MM = 5.0;
        constexpr float PARK_RETRACT_MM = 2.0f;
    }

    PrintJobManager::PrintJobManager(std::shared_ptr<DriverInterface> driver,
                                     std::shared_ptr<core::CommandExecutorQueue> commandQueue)
            : driver_(driver), commandQueue_(commandQueue), currentState_(JobState::CREATED) {
        if (commandQueue_) {
            // Guasto di comunicazione/macchina: la coda e' gia' stata svuotata, il job e' fallito
            commandQueue_->setJobFaultCallback([this](const std::string &jobId, const std::string &reason) {
                std::lock_guard<std::mutex> lock(stateMutex_);
                if (jobId.empty() || jobId != currentJobId_) return;
                Logger::logError("[PrintJobManager] Job " + jobId + " stopped: " + reason);
                commandQueue_->setPaused(false);
                updateState(JobState::FAILED);
            });

            // Ultimo comando eseguito: il firmware torna Idle, pronto per il job successivo
            commandQueue_->setJobFinishedCallback([this](const std::string &jobId) {
                std::lock_guard<std::mutex> lock(stateMutex_);
                if (jobId.empty() || jobId != currentJobId_) return;
                if (currentState_ != JobState::RUNNING) return;
                driver_->system()->endPrint(); // attende la fine dei movimenti in coda sul firmware
                JobCheckpoint::remove(jobId);
                updateState(JobState::COMPLETED);
                Logger::logInfo("[PrintJobManager] Job completed: " + jobId);
            });
        }
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

        // Lo stato Printing del firmware viene richiesto dal primo movimento: il G28
        // iniziale del G-code deve trovare la macchina in Idle.
        if (driver_->getState() == PrintState::Printing || driver_->getState() == PrintState::Paused) {
            driver_->system()->endPrint();
        }

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
            // Prima si ferma la coda: i comandi restano in attesa invece di essere
            // inviati (e rifiutati) a macchina in pausa.
            commandQueue_->setPaused(true);
            // Il firmware completa i pochi segmenti che ha gia' in coda e si ferma da solo
            driver_->waitForIdle(std::chrono::seconds(60));

            parked_ = false;
            if (driver_->getState() == PrintState::Printing) {
                // Parcheggio: ritrae il filamento e solleva l'ugello, cosi' non resta
                // caldo e in pressione sul pezzo per tutta la pausa.
                driver_->extruder()->retract(PARK_RETRACT_MM, 1800.0f);
                const auto lift = driver_->motion()->moveRelative(
                        {std::nullopt, std::nullopt, PARK_LIFT_MM, std::nullopt, 600.0});
                parked_ = lift.isSuccess();
                driver_->system()->pause(); // risponde quando la macchina e' ferma
            }
            commandQueue_->saveCheckpointNow();
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
            // Non in pausa: se esiste un checkpoint e' una ripresa dopo un'interruzione
            return recoverLocked(currentJobId_);
        }

        try {
            if (driver_->getState() == PrintState::Paused) {
                const auto result = driver_->system()->resume();
                if (!result.isSuccess()) {
                    Logger::logError("[PrintJobManager] Resume rejected: " + result.message);
                    return false;
                }
            }
            if (parked_) {
                // Ritorno esatto al punto di pausa: stessa quota, filamento di nuovo in pressione
                driver_->motion()->moveRelative({std::nullopt, std::nullopt, -PARK_LIFT_MM, std::nullopt, 600.0});
                driver_->extruder()->extrude(PARK_RETRACT_MM, 1800.0f);
                driver_->waitForIdle(std::chrono::seconds(60));
                parked_ = false;
            }
            commandQueue_->setPaused(false);
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

    bool PrintJobManager::recoverJob(const std::string &jobId) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return recoverLocked(jobId);
    }

    std::string PrintJobManager::lastRecoveryError() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return lastRecoveryError_;
    }

    bool PrintJobManager::failRecovery(const std::string &reason) {
        lastRecoveryError_ = reason;
        Logger::logError("[PrintJobManager] Recovery failed: " + reason);
        return false;
    }

    bool PrintJobManager::recoverLocked(const std::string &jobId) {
        lastRecoveryError_.clear();

        if (currentState_ == JobState::RUNNING || currentState_ == JobState::PAUSED ||
            currentState_ == JobState::LOADING) {
            return failRecovery("a job is already active");
        }

        const auto checkpoint = jobId.empty() ? JobCheckpoint::latest() : JobCheckpoint::load(jobId);
        if (!checkpoint) {
            return failRecovery("no checkpoint found");
        }
        if (!std::filesystem::exists(checkpoint->gcodePath)) {
            return failRecovery("g-code file no longer available: " + checkpoint->gcodePath);
        }

        // --- collegamento e stato del firmware ---
        driver_->system()->clearError(); // se serve risincronizza anche la numerazione
        if (!driver_->waitForIdle(std::chrono::seconds(30))) {
            return failRecovery("firmware not responding");
        }
        driver_->system()->endPrint(); // idempotente: porta il firmware in Idle da Printing/Paused
        if (driver_->getState() != PrintState::Idle) {
            return failRecovery("firmware not idle: " + DriverInterface::printStateToString(driver_->getState()));
        }

        bool referenced = false;
        if (!driver_->motion()->getPosition(&referenced)) {
            return failRecovery("cannot read machine position");
        }

        if (!referenced) {
            // Il firmware si e' riavviato: ha perso la posizione
            if (!checkpoint->positionKnown) {
                return failRecovery("firmware restarted and the checkpoint has no position");
            }
            const auto homing = driver_->system()->homeAxes("XY");
            bool xHomed = false, yHomed = false;
            for (const auto &line: homing.body) {
                if (line == "HOM X OK") xHomed = true;
                if (line == "HOM Y OK") yHomed = true;
            }
            if (!homing.isSuccess() || !xHomed || !yHomed) {
                return failRecovery("firmware restarted: X/Y endstops are required to find the position again");
            }
            // Z non si puo' azzerare col pezzo sul piano: si assume che non si sia mosso
            const auto z = driver_->motion()->setPosition(
                    {std::nullopt, std::nullopt, checkpoint->z, std::nullopt, std::nullopt});
            if (!z.isSuccess()) {
                return failRecovery("cannot restore Z position: " + z.message);
            }
        }

        // --- ripristino dello stato, espresso in G-code cosi' che anche il traduttore si riallinei ---
        using core::utils::formatFloat;
        std::vector<std::string> preamble;
        if (checkpoint->hotendTarget > 0.0) {
            preamble.push_back("M109 S" + formatFloat(checkpoint->hotendTarget, 0));
        }
        preamble.push_back(checkpoint->fanSpeed > 0 ? "M106 S" + std::to_string(checkpoint->fanSpeed) : "M107");
        preamble.push_back("G90");
        preamble.push_back("M82");
        preamble.push_back("G92 E" + formatFloat(checkpoint->e, 4));
        if (checkpoint->positionKnown) {
            // si arriva dall'alto, per non strisciare sul pezzo
            preamble.push_back("G1 Z" + formatFloat(checkpoint->z + PARK_LIFT_MM, 3) + " F600");
            preamble.push_back("G1 X" + formatFloat(checkpoint->x, 3) + " Y" + formatFloat(checkpoint->y, 3) +
                               " F6000");
            preamble.push_back("G1 Z" + formatFloat(checkpoint->z, 3) + " F600");
        }
        if (!checkpoint->absoluteXYZ) preamble.push_back("G91"); // imposta anche E relativo
        preamble.push_back(checkpoint->absoluteE ? "M82" : "M83");
        preamble.push_back("G1 F" + formatFloat(checkpoint->feedrate, 0));

        currentJobId_ = checkpoint->jobId;
        currentFilePath_ = checkpoint->gcodePath;
        startTime_ = std::chrono::steady_clock::now();
        parked_ = false;

        commandQueue_->setPaused(false);
        commandQueue_->enqueueFile(checkpoint->gcodePath, 3, checkpoint->jobId, checkpoint->nextLine, preamble);
        updateState(JobState::RUNNING);

        Logger::logInfo("[PrintJobManager] Recovering job " + checkpoint->jobId + " from line " +
                        std::to_string(checkpoint->nextLine));
        return true;
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
            commandQueue_->forgetJob(currentJobId_); // un job annullato non si riprende
            commandQueue_->clearQueue();
            commandQueue_->setPaused(false);
        }

        // Ferma subito il movimento in corso, senza aspettare che finisca
        driver_->abortMotion();

        // Update job tracker
        auto &jobTracker = jobs::JobTracker::getInstance();
        jobTracker.cancelJob(currentJobId_);
        JobCheckpoint::remove(currentJobId_);

        // Emergency stop
        try {
            driver_->motion()->emergencyStop();
            driver_->temperature()->setHotendTemperature(0);
            driver_->system()->endPrint();
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

