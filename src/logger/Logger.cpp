//
// Created by redeg on 26/04/2025.
//

#include "logger/Logger.hpp"

#include <algorithm>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <thread>
#include <atomic>
#include <vector>

namespace fs = std::filesystem;

std::ofstream Logger::logFile_;
std::mutex Logger::logMutex_;
std::string Logger::currentLogPath_;
std::atomic<size_t> Logger::currentLogSize_{0};
std::atomic<bool> Logger::rotationEnabled_{true};
std::thread Logger::cleanupThread_;
std::atomic<bool> Logger::shutdownRequested_{false};

constexpr size_t MAX_LOG_SIZE = 50 * 1024 * 1024; // 50MB
constexpr size_t MAX_LOG_FILES = 10;
constexpr std::chrono::hours LOG_RETENTION{24 * 7}; // 7 days

void Logger::init() {
    rotateLogFile();
    startCleanupThread();
    std::cout << "[Logger] Initialized with auto-rotation (max " << MAX_LOG_SIZE / 1024 / 1024 << "MB)" << std::endl;
}

void Logger::shutdown() {
    shutdownRequested_ = true;
    if (cleanupThread_.joinable()) {
        cleanupThread_.join();
    }
    std::lock_guard<std::mutex> lock(logMutex_);
    if (logFile_.is_open()) {
        logFile_.close();
    }
}

void Logger::logInfo(const std::string &message) {
    log("INFO", message);
}

void Logger::logWarning(const std::string &message) {
    log("WARNING", message);
}

void Logger::logError(const std::string &message) {
    log("ERROR", message);
}

void Logger::log(const std::string &level, const std::string &message) {
    if (message.empty() || message.find_first_not_of(" \t\r\n") == std::string::npos) {
        return;
    }

    std::string timestamp = currentTimestamp();
    std::string formatted = "[" + level + "] [" + timestamp + "] " + message;

    if (level == "ERROR") {
        std::cerr << formatted << std::endl;
    } else {
        std::cout << formatted << std::endl;
    }

    std::lock_guard<std::mutex> lock(logMutex_);
    if (rotationEnabled_ && currentLogSize_ > MAX_LOG_SIZE) {
        rotateLogFile();
    }

    if (logFile_.is_open()) {
        logFile_ << formatted << std::endl;
        currentLogSize_ += formatted.length() + 1;
    }
}

void Logger::rotateLogFile() {
    if (logFile_.is_open()) {
        logFile_.close();
    }

    currentLogPath_ = generateLogFilename();
    logFile_.open(currentLogPath_, std::ios::out | std::ios::trunc);
    currentLogSize_ = 0;

    if (!logFile_.is_open()) {
        std::cerr << "[Logger] ERROR: Cannot open log file: " << currentLogPath_ << std::endl;
    }
}

void Logger::startCleanupThread() {
    cleanupThread_ = std::thread([]() {
        while (!shutdownRequested_) {
            cleanupOldLogs();
            std::this_thread::sleep_for(std::chrono::hours(1));
        }
    });
}

void Logger::cleanupOldLogs() {
    try {
        std::string logsFolder = "logs";
        if (!fs::exists(logsFolder)) return;

        auto cutoffTime = std::chrono::system_clock::now() - LOG_RETENTION;
        std::vector<fs::path> logFiles;

        for (const auto &entry: fs::directory_iterator(logsFolder)) {
            if (entry.path().extension() == ".log") {
                auto writeTime = fs::last_write_time(entry);
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    writeTime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());

                if (sctp < cutoffTime) {
                    fs::remove(entry);
                } else {
                    logFiles.push_back(entry.path());
                }
            }
        }

        if (logFiles.size() > MAX_LOG_FILES) {
            std::sort(logFiles.begin(), logFiles.end(), [](const fs::path &a, const fs::path &b) {
                return fs::last_write_time(a) < fs::last_write_time(b);
            });

            for (size_t i = 0; i < logFiles.size() - MAX_LOG_FILES; ++i) {
                fs::remove(logFiles[i]);
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "[Logger] Cleanup error: " << e.what() << std::endl;
    }
}

std::string Logger::currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string Logger::generateLogFilename() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S");

    std::string logsFolder = "logs";
    if (!fs::exists(logsFolder)) {
        fs::create_directory(logsFolder);
    }

    return logsFolder + "/3dp_driver_" + ss.str() + ".log";
}
