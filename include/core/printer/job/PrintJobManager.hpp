#pragma once

#include "core/DriverInterface.hpp"
#include "core/queue/CommandExecutorQueue.hpp"
#include "PrintJobState.hpp"
#include "PrintJobProgress.hpp"
#include "GCodeDownloader.hpp"  // Include completo invece di forward declaration
#include <string>
#include <chrono>
#include <atomic>
#include <mutex>
#include <memory>
#include <fstream>

namespace core::print {
    class PrintJobManager {
    public:
        PrintJobManager(std::shared_ptr<DriverInterface> driver,
                        std::shared_ptr<CommandExecutorQueue> commandQueue);

        // Job control
        bool startPrintJob(const std::string &gcodePath, const std::string &jobId);

        bool startPrintJobFromUrl(const std::string &gcodeUrl, const std::string &jobId);

        bool pauseJob();

        bool resumeJob();

        bool cancelJob();

        // Status
        PrintJobState getCurrentState() const;

        PrintJobProgress getProgress() const;

        std::string stateToString(core::print::PrintJobState state) const;

        // Safety
        bool isReadyToPrint() const;

    private:
        std::shared_ptr<DriverInterface> driver_;
        std::shared_ptr<CommandExecutorQueue> commandQueue_;
        mutable std::mutex stateMutex_;
        PrintJobState currentState_;
        std::string currentJobId_;
        std::string currentFilePath_;

        // Progress tracking
        std::atomic<size_t> totalLines_{0};
        std::atomic<size_t> executedLines_{0};
        std::chrono::steady_clock::time_point startTime_;

        std::unique_ptr<GCodeDownloader> downloader_;

        void onDownloadProgress(const DownloadProgress &progress);

        void onDownloadCompleted(bool success, const std::string &filePath, const std::string &error);

        // Safety checks
        bool checkTemperatures() const;

        bool checkHoming() const;

        bool checkEndstops() const;

        void updateState(PrintJobState newState);

        void resetJob();
    };
} // namespace core::print
