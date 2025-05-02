//
// Created by redeg on 26/04/2025.
//

#include "logger/Logger.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

std::ofstream Logger::logFile_;

void Logger::init() {
    std::string filename = generateLogFilename();
    logFile_.open(filename, std::ios::out | std::ios::trunc);
    if (!logFile_.is_open()) {
        std::cerr << "[Logger] ERROR: Cannot open log file: " << filename << std::endl;
    } else {
        std::cout << "[Logger] Logging to: " << filename << std::endl;
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

    if (logFile_.is_open()) {
        logFile_ << formatted << std::endl;
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

    std::stringstream dateStream;
    dateStream << std::put_time(std::localtime(&in_time_t), "%d-%m-%Y");

    std::string baseName = dateStream.str();
    std::string logsFolder = "logs";

    // Se la cartella logs/ non esiste, creala
    if (!fs::exists(logsFolder)) {
        fs::create_directory(logsFolder);
    }

    for (int i = 0; i < 1000; ++i) {
        std::stringstream ss;
        ss << logsFolder << "/" << baseName << "-(" << i << ").log";
        std::string filename = ss.str();
        if (!fs::exists(filename)) {
            return filename;
        }
    }

    throw std::runtime_error("Too many log files for today!");
}
