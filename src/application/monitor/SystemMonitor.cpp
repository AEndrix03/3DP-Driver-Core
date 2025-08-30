#include "application/monitor/SystemMonitor.hpp"
#include "logger/Logger.hpp"
#include <chrono>
#include <utility>

SystemMonitor::SystemMonitor(std::unique_ptr<connector::controllers::HeartbeatController> &heartbeatController,
                             std::unique_ptr<connector::controllers::PrinterCommandController> &
                             printerCommandController,
                             std::unique_ptr<connector::controllers::PrinterCheckController> &printerCheckController,
                             std::unique_ptr<connector::controllers::PrinterControlController> &
                             printerControlController,
                             std::shared_ptr<core::RealPrinter> printer,
                             std::shared_ptr<core::CommandExecutorQueue> commandQueue_)
        : heartbeatController_(heartbeatController),
          printerCommandController_(printerCommandController),
          printerCheckController_(printerCheckController),
          printerControlController_(printerControlController),
          printer_(std::move(printer)),
          commandQueue_(std::move(commandQueue_)) {
}

SystemMonitor::~SystemMonitor() {
    stop();
}

void SystemMonitor::start() {
    if (running_) {
        Logger::logWarning("[SystemMonitor] Already running");
        return;
    }

    running_ = true;
    monitorThread_ = std::thread([this]() {
        try {
            monitorLoop();
        } catch (const std::exception &e) {
            Logger::logError("[SystemMonitor] Monitor thread crashed: " + std::string(e.what()));
        }
    });

    Logger::logInfo("[SystemMonitor] Started");
}

void SystemMonitor::stop() {
    if (!running_) return;

    running_ = false;
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }

    Logger::logInfo("[SystemMonitor] Stopped");
}

bool SystemMonitor::isRunning() const {
    return running_;
}

void SystemMonitor::monitorLoop() {
    Logger::logInfo("[SystemMonitor] Monitor loop started");
    int counter = 0;

    while (running_) {
        try {
            // Report stats every 30 seconds
            if (++counter % 30 == 0) {
                reportKafkaStats();
                counter = 0;
            }
        } catch (const std::exception &e) {
            Logger::logError("[SystemMonitor] Loop error: " + std::string(e.what()));
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void SystemMonitor::reportKafkaStats() const {
    Logger::logInfo("[SystemMonitor] ===== System Status Report =====");

    // Command Queue Status - CRITICAL
    Logger::logInfo("[SystemMonitor] Command Executor Queue:");
    if (commandQueue_) {
        bool isRunning = commandQueue_->isRunning();
        auto stats = commandQueue_->getStatistics();
        Logger::logInfo("  Running: " + std::string(isRunning ? "TRUE" : "FALSE"));
        Logger::logInfo("  Total Enqueued: " + std::to_string(stats.totalEnqueued));
        Logger::logInfo("  Total Executed: " + std::to_string(stats.totalExecuted));
        Logger::logInfo("  Current Queue Size: " + std::to_string(stats.currentQueueSize));
        Logger::logInfo("  Errors: " + std::to_string(stats.totalErrors));
        Logger::logInfo("  Disk Operations: " + std::to_string(stats.diskOperations));

        if (!isRunning && stats.currentQueueSize > 0) {
            Logger::logError("[SystemMonitor] WARNING: Queue has commands but is not running!");
        }
    } else {
        Logger::logError("[SystemMonitor] Command Queue: NOT AVAILABLE");
    }

    // Heartbeat Controller
    if (heartbeatController_) {
        auto stats = heartbeatController_->getStatistics();
        bool isRunning = heartbeatController_->isRunning();

        Logger::logInfo("[SystemMonitor] Heartbeat Status:");
        Logger::logInfo("  Running: " + std::string(isRunning ? "true" : "false"));
        Logger::logInfo("  Messages RX: " + std::to_string(stats.messagesReceived));
        Logger::logInfo("  Messages TX: " + std::to_string(stats.messagesSent));
        Logger::logInfo("  Errors: " + std::to_string(stats.processingErrors));
    } else {
        Logger::logInfo("[SystemMonitor] Heartbeat Controller: NOT AVAILABLE");
    }

    // Printer Command Controller
    if (printerCommandController_) {
        auto stats = printerCommandController_->getStatistics();
        bool isRunning = printerCommandController_->isRunning();

        Logger::logInfo("[SystemMonitor] PrinterCommand Status:");
        Logger::logInfo("  Running: " + std::string(isRunning ? "true" : "false"));
        Logger::logInfo("  Messages RX: " + std::to_string(stats.messagesReceived));
        Logger::logInfo("  Messages TX: " + std::to_string(stats.messagesSent));
        Logger::logInfo("  Errors: " + std::to_string(stats.processingErrors));

        if (stats.messagesReceived > 0 && stats.messagesProcessed == 0) {
            Logger::logWarning("[SystemMonitor] WARNING: Commands received but not processed!");
        }
    } else {
        Logger::logInfo("[SystemMonitor] PrinterCommand Controller: NOT AVAILABLE");
    }

    // Printer Check Controller
    if (printerCheckController_) {
        auto stats = printerCheckController_->getStatistics();
        bool isRunning = printerCheckController_->isRunning();

        Logger::logInfo("[SystemMonitor] PrinterCheck Status:");
        Logger::logInfo("  Running: " + std::string(isRunning ? "true" : "false"));
        Logger::logInfo("  Messages RX: " + std::to_string(stats.messagesReceived));
        Logger::logInfo("  Messages TX: " + std::to_string(stats.messagesSent));
        Logger::logInfo("  Errors: " + std::to_string(stats.processingErrors));
    } else {
        Logger::logInfo("[SystemMonitor] PrinterCheck Controller: NOT AVAILABLE");
    }

    // Printer Control Controller
    if (printerControlController_) {
        auto stats = printerControlController_->getStatistics();
        bool isRunning = printerControlController_->isRunning();

        Logger::logInfo("[SystemMonitor] PrinterControl Status:");
        Logger::logInfo("  Running: " + std::string(isRunning ? "true" : "false"));
        Logger::logInfo("  Start Requests: " + std::to_string(stats.startRequests));
        Logger::logInfo("  Stop Requests: " + std::to_string(stats.stopRequests));
        Logger::logInfo("  Pause Requests: " + std::to_string(stats.pauseRequests));
        Logger::logInfo("  Errors: " + std::to_string(stats.processingErrors));
    } else {
        Logger::logInfo("[SystemMonitor] PrinterControl Controller: NOT AVAILABLE");
    }

    Logger::logInfo("[SystemMonitor] =======================================");
}
