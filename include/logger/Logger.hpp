//
// Created by redeg on 26/04/2025.
//

#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <thread>
#include <atomic>

class Logger {
public:
    static void init();

    static void shutdown();

    static void logInfo(const std::string &message);

    static void logWarning(const std::string &message);

    static void logError(const std::string &message);

    /**
     * Traccia dettagliata (ogni riga inviata e ricevuta sulla seriale). Spenta di default:
     * in stampa sono centinaia di righe al secondo. Si accende con la variabile
     * d'ambiente DRIVER_SERIAL_TRACE=1.
     */
    static void logTrace(const std::string &message);

    static bool isTraceEnabled() { return traceEnabled_.load(std::memory_order_relaxed); }

private:
    static std::atomic<bool> traceEnabled_;
    static std::ofstream logFile_;
    static std::mutex logMutex_;
    static std::string currentLogPath_;
    static std::atomic<size_t> currentLogSize_;
    static std::atomic<bool> rotationEnabled_;
    static std::thread cleanupThread_;
    static std::atomic<bool> shutdownRequested_;

    static void log(const std::string &level, const std::string &message);

    static void rotateLogFile();

    static void startCleanupThread();

    static void cleanupOldLogs();

    static std::string currentTimestamp();

    static std::string generateLogFilename();
};
