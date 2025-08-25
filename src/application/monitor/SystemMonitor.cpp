#include "application/monitor/SystemMonitor.hpp"
#include "logger/Logger.hpp"
#include <chrono>

SystemMonitor::SystemMonitor(std::unique_ptr<connector::controllers::HeartbeatController> &heartbeatController,
                             std::unique_ptr<connector::controllers::PrinterCommandController> &
                             printerCommandController,
                             std::unique_ptr<connector::controllers::PrinterCheckController> &printerCheckController,
                             std::shared_ptr<core::RealPrinter> printer)
    : heartbeatController_(heartbeatController),
      printerCommandController_(printerCommandController),
      printerCheckController_(printerCheckController),
      printer_(printer) {
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

void SystemMonitor::checkHardwareStatus() {
    // Removed - not needed and causes blocking
}

void SystemMonitor::reportKafkaStats() {
    Logger::logInfo("[SystemMonitor] ===== Kafka Controllers Status =====");

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

    Logger::logInfo("[SystemMonitor] =======================================");
}
