//
// Created by Andrea on 27/08/2025.
//

#include "core/printer/job/tracking/JobTracker.hpp"
#include "logger/Logger.hpp"
#include <algorithm>
#include <optional>
#include <vector>

namespace core::jobs {
    JobTracker &JobTracker::getInstance() {
        static JobTracker instance;
        return instance;
    }

    void JobTracker::startJob(const std::string &jobId, size_t totalCommands) {
        std::lock_guard<std::mutex> lock(jobsMutex_);

        JobInfo info;
        info.jobId = jobId;
        info.state = core::print::JobState::RUNNING;
        info.startTime = std::chrono::steady_clock::now();
        info.lastUpdate = info.startTime;
        info.totalCommands = totalCommands;
        info.executedCommands = 0;

        jobs_[jobId] = std::move(info);
        currentJobId_ = jobId;
        stats_.totalJobs++;

        Logger::logInfo("[JobTracker] Started job: " + jobId +
                        " (" + std::to_string(totalCommands) + " commands)");
    }

    void JobTracker::updateJobProgress(const std::string &jobId, const std::string &currentCommand) {
        std::lock_guard<std::mutex> lock(jobsMutex_);

        auto it = jobs_.find(jobId);
        if (it == jobs_.end()) return;

        it->second.executedCommands++;
        it->second.currentCommand = currentCommand;
        it->second.lastUpdate = std::chrono::steady_clock::now();

        // Auto-complete if all commands executed
        if (it->second.executedCommands >= it->second.totalCommands &&
            it->second.state == core::print::JobState::RUNNING) {
            updateJobState(jobId, core::print::JobState::COMPLETED);
        }
    }

    void JobTracker::completeJob(const std::string &jobId) {
        std::lock_guard<std::mutex> lock(jobsMutex_);
        updateJobState(jobId, core::print::JobState::COMPLETED);
        stats_.completedJobs++;
        if (currentJobId_ == jobId) currentJobId_.clear();
        Logger::logInfo("[JobTracker] Completed job: " + jobId);
    }

    void JobTracker::failJob(const std::string &jobId, const std::string &error) {
        std::lock_guard<std::mutex> lock(jobsMutex_);
        auto it = jobs_.find(jobId);
        if (it != jobs_.end()) {
            it->second.error = error;
            updateJobState(jobId, core::print::JobState::FAILED);
        }
        stats_.failedJobs++;
        if (currentJobId_ == jobId) currentJobId_.clear();
        Logger::logError("[JobTracker] Failed job: " + jobId + " - " + error);
    }

    void JobTracker::pauseJob(const std::string &jobId) {
        std::lock_guard<std::mutex> lock(jobsMutex_);
        updateJobState(jobId, core::print::JobState::PAUSED);
        Logger::logInfo("[JobTracker] Paused job: " + jobId);
    }

    void JobTracker::resumeJob(const std::string &jobId) {
        std::lock_guard<std::mutex> lock(jobsMutex_);
        updateJobState(jobId, core::print::JobState::RUNNING);
        Logger::logInfo("[JobTracker] Resumed job: " + jobId);
    }

    void JobTracker::cancelJob(const std::string &jobId) {
        std::lock_guard<std::mutex> lock(jobsMutex_);
        updateJobState(jobId, core::print::JobState::CANCELLED);
        stats_.cancelledJobs++;
        if (currentJobId_ == jobId) currentJobId_.clear();
        Logger::logInfo("[JobTracker] Cancelled job: " + jobId);
    }

    std::optional<JobInfo> JobTracker::getJobInfo(const std::string &jobId) const {
        std::lock_guard<std::mutex> lock(jobsMutex_);
        auto it = jobs_.find(jobId);
        return (it != jobs_.end()) ? std::optional<JobInfo>(it->second) : std::nullopt;
    }

    std::string JobTracker::getJobStateCode(const std::string &jobId) const {
        std::lock_guard<std::mutex> lock(jobsMutex_);
        auto it = jobs_.find(jobId);
        if (it == jobs_.end()) return "UNK";
        return core::print::jobStateToCode(it->second.state);
    }

    std::vector<JobInfo> JobTracker::getActiveJobs() const {
        std::lock_guard<std::mutex> lock(jobsMutex_);
        std::vector<JobInfo> active;

        for (const auto &[id, info]: jobs_) {
            if (info.state == core::print::JobState::RUNNING ||
                info.state == core::print::JobState::PAUSED ||
                info.state == core::print::JobState::LOADING ||
                info.state == core::print::JobState::HEATING) {
                active.push_back(info);
            }
        }

        return active;
    }

    bool JobTracker::hasActiveJob() const {
        return !getCurrentJobId().empty();
    }

    std::string JobTracker::getCurrentJobId() const {
        std::lock_guard<std::mutex> lock(jobsMutex_);
        return currentJobId_;
    }

    JobTracker::Statistics JobTracker::getStatistics() const {
        std::lock_guard<std::mutex> lock(jobsMutex_);
        return stats_;
    }

    void JobTracker::updateJobState(const std::string &jobId, core::print::JobState newState) {
        auto it = jobs_.find(jobId);
        if (it != jobs_.end()) {
            it->second.state = newState;
            it->second.lastUpdate = std::chrono::steady_clock::now();
        }
    }

    void JobTracker::cleanupCompletedJobs() {
        // Keep only last 100 completed jobs to prevent memory leak
        constexpr size_t MAX_COMPLETED_JOBS = 100;

        std::vector<std::pair<std::string, std::chrono::steady_clock::time_point> > completed;
        for (const auto &[id, info]: jobs_) {
            if (info.state == core::print::JobState::COMPLETED ||
                info.state == core::print::JobState::FAILED ||
                info.state == core::print::JobState::CANCELLED) {
                completed.emplace_back(id, info.lastUpdate);
            }
        }

        if (completed.size() > MAX_COMPLETED_JOBS) {
            std::sort(completed.begin(), completed.end(),
                      [](const auto &a, const auto &b) { return a.second < b.second; });

            size_t toRemove = completed.size() - MAX_COMPLETED_JOBS;
            for (size_t i = 0; i < toRemove; ++i) {
                jobs_.erase(completed[i].first);
            }
        }
    }
} // namespace core::jobs
