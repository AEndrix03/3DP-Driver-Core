//
// Created by Andrea on 27/08/2025.
//

#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <optional>
#include <vector>

#include "core/printer/job/PrintJobState.hpp"

namespace core::jobs {
    struct JobInfo {
        std::string jobId;
        core::print::JobState state;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point lastUpdate;
        size_t totalCommands = 0;
        size_t executedCommands = 0;
        std::string currentCommand;
        std::string error;

        float getProgress() const {
            if (totalCommands == 0) return 0.0f;
            return (float(executedCommands) / totalCommands) * 100.0f;
        }

        std::chrono::seconds getElapsedTime() const {
            auto now = std::chrono::steady_clock::now();
            return std::chrono::duration_cast<std::chrono::seconds>(now - startTime);
        }
    };

    class JobTracker {
    public:
        static JobTracker &getInstance();

        JobTracker() = default;

        ~JobTracker() = default;

        // Job lifecycle
        void startJob(const std::string &jobId, size_t totalCommands);

        void updateJobProgress(const std::string &jobId, const std::string &currentCommand);

        void completeJob(const std::string &jobId);

        void failJob(const std::string &jobId, const std::string &error);

        void pauseJob(const std::string &jobId);

        void resumeJob(const std::string &jobId);

        void cancelJob(const std::string &jobId);

        // Query methods
        std::optional<JobInfo> getJobInfo(const std::string &jobId) const;

        std::string getJobStateCode(const std::string &jobId) const;

        std::vector<JobInfo> getActiveJobs() const;

        bool hasActiveJob() const;

        std::string getCurrentJobId() const;

        // Statistics
        struct Statistics {
            size_t totalJobs = 0;
            size_t completedJobs = 0;
            size_t failedJobs = 0;
            size_t cancelledJobs = 0;
        };

        Statistics getStatistics() const;

    private:
        mutable std::mutex jobsMutex_;
        std::unordered_map<std::string, JobInfo> jobs_;
        std::string currentJobId_;
        mutable Statistics stats_;

        void updateJobState(const std::string &jobId, core::print::JobState newState);

        void cleanupCompletedJobs();
    };
} // namespace core::jobs
