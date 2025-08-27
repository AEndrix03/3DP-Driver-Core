//
// Created by Andrea on 27/08/2025.
//

#include "connector/processors/printer-control/PrinterControlProcessor.hpp"
#include "logger/Logger.hpp"
#include "core/printer/job/tracking/JobTracker.hpp"
#include <chrono>
#include <sstream>
#include <iomanip>

namespace connector::processors::printer_control {
    PrinterControlProcessor::PrinterControlProcessor(
        std::shared_ptr<core::DriverInterface> driver,
        std::shared_ptr<core::CommandExecutorQueue> commandQueue,
        std::shared_ptr<core::print::PrintJobManager> jobManager)
        : driver_(driver), commandQueue_(commandQueue), jobManager_(jobManager) {
    }

    void PrinterControlProcessor::processPrinterStartRequest(
        const models::printer_control::PrinterStartRequest &request) {
        Logger::logInfo("[PrinterControlProcessor] Processing start request for driver: " + request.driverId);
        try {
            std::string jobId = generateJobId(request.driverId);
            // Execute pre-print G-code if provided
            if (!request.startGCode.empty()) {
                Logger::logInfo("[PrinterControlProcessor] Executing start G-code");
                executeGCodeSequence(request.startGCode, jobId);
            }
            // Start main print job
            bool success = false;
            if (!request.gcodeUrl.empty()) {
                Logger::logInfo("[PrinterControlProcessor] Starting print from URL: " + request.gcodeUrl);
                success = jobManager_->startPrintJobFromUrl(request.gcodeUrl, jobId);
            } else if (!request.startGCode.empty()) {
                Logger::logInfo("[PrinterControlProcessor] Starting print from inline G-code");
                // For inline G-code, we already executed it above
                success = true;
            }
            if (success) {
                Logger::logInfo("[PrinterControlProcessor] Print job started successfully: " + jobId);
            } else {
                Logger::logError("[PrinterControlProcessor] Failed to start print job: " + jobId);
                auto &jobTracker = core::jobs::JobTracker::getInstance();
                jobTracker.failJob(jobId, "START_FAILED");
            }
        } catch (const std::exception &e) {
            Logger::logError("[PrinterControlProcessor] Start request failed: " + std::string(e.what()));
        }
    }

    void PrinterControlProcessor::processPrinterStopRequest(
        const models::printer_control::PrinterStopRequest &request) const {
        Logger::logInfo("[PrinterControlProcessor] Processing stop request for driver: " + request.driverId);
        try {
            // Cancel current job
            if (!jobManager_->cancelJob()) {
                Logger::logWarning("[PrinterControlProcessor] No active job to cancel");
            }
            // Emergency stop
            auto result = driver_->motion()->emergencyStop();
            if (result.isSuccess()) {
                Logger::logInfo("[PrinterControlProcessor] Emergency stop executed successfully");
            } else {
                Logger::logError("[PrinterControlProcessor] Emergency stop failed: " + result.message);
            }
        } catch (const std::exception &e) {
            Logger::logError("[PrinterControlProcessor] Stop request failed: " + std::string(e.what()));
        }
    }

    void PrinterControlProcessor::processPrinterPauseRequest(
        const models::printer_control::PrinterPauseRequest &request) const {
        Logger::logInfo("[PrinterControlProcessor] Processing pause request for driver: " + request.driverId);
        try {
            if (!jobManager_->pauseJob()) {
                Logger::logWarning("[PrinterControlProcessor] No active job to pause or job already paused");
            } else {
                Logger::logInfo("[PrinterControlProcessor] Job paused successfully");
            }
        } catch (const std::exception &e) {
            Logger::logError("[PrinterControlProcessor] Pause request failed: " + std::string(e.what()));
        }
    }

    void PrinterControlProcessor::executeGCodeSequence(const std::string &gcode, const std::string &jobId) const {
        // Split G-code by lines and enqueue with high priority
        std::istringstream stream(gcode);
        std::string line;
        std::vector<std::string> commands;
        while (std::getline(stream, line)) {
            if (!line.empty() && line[0] != ';' && line.find_first_not_of(" \t\r\n") != std::string::npos) {
                commands.push_back(line);
            }
        }

        if (!commands.empty()) {
            // High priority (1) for control commands
            commandQueue_->enqueueCommands(commands, 1, jobId);
            Logger::logInfo(
                "[PrinterControlProcessor] Enqueued " + std::to_string(commands.size()) + " control commands");
        }
    }

    std::string PrinterControlProcessor::generateJobId(const std::string &driverId) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);

        std::ostringstream oss;
        oss << driverId << "_job_" << std::put_time(std::gmtime(&time_t), "%Y%m%d_%H%M%S");
        return oss.str();
    }
}
