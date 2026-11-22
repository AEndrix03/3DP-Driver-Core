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
                        std::shared_ptr<core::CommandExecutorQueue> commandQueue);

        // Job control
        bool startPrintJob(const std::string &gcodePath, const std::string &jobId);

        bool startPrintJobFromUrl(const std::string &gcodeUrl, const std::string &jobId);

        bool pauseJob();

        bool resumeJob();

        bool cancelJob();

        /// G-code da eseguire alla fine del prossimo job avviato (es. spegnimento heater, parcheggio).
        void setEndGCode(const std::string &endGCode);

        /**
         * @brief Riprende un job interrotto (guasto, riavvio del firmware o del driver) dal suo
         * ultimo checkpoint: ripristina temperatura, ventola e modalita', torna al punto di
         * interruzione e prosegue dalla prima riga non completata.
         *
         * Se il firmware si e' riavviato la posizione va ritrovata: X e Y vengono riazzerati
         * sui finecorsa (che devono essere abilitati), Z viene assunto fermo dove era.
         *
         * @param jobId job da riprendere; vuoto = il checkpoint piu' recente
         */
        bool recoverJob(const std::string &jobId = "");

        /// Motivo dell'ultimo recoverJob() fallito.
        std::string lastRecoveryError() const;

        // Status
        JobState getCurrentState() const;

        PrintJobProgress getProgress() const;

        std::string stateToString(JobState state) const;

        // Safety
        bool isReadyToPrint() const;

    private:
        std::shared_ptr<DriverInterface> driver_;
        std::shared_ptr<core::CommandExecutorQueue> commandQueue_;
        mutable std::mutex stateMutex_;
        JobState currentState_;
        std::string currentJobId_;
        std::string currentFilePath_;

        // Progress tracking
        std::atomic<size_t> totalLines_{0};
        std::atomic<size_t> executedLines_{0};
        std::chrono::steady_clock::time_point startTime_;

        std::unique_ptr<GCodeDownloader> downloader_;

        bool startPrintJobInternal(const std::string &gcodePath, const std::string &jobId);

        void onDownloadProgress(const DownloadProgress &progress);

        void onDownloadCompleted(bool success, const std::string &filePath, const std::string &error);

        // Safety checks
        bool checkTemperatures() const;

        bool checkHoming() const;

        bool checkEndstops() const;

        void updateState(JobState newState);

        void resetJob();

        bool recoverLocked(const std::string &jobId);

        bool failRecovery(const std::string &reason);

        bool parked_ = false;            // ugello sollevato e filamento ritratto durante la pausa
        std::string lastRecoveryError_;
        std::string endGCode_;
    };
} // namespace core::print
