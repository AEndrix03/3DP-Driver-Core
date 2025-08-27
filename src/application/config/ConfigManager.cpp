//
// Created by Andrea on 27/08/2025.
//

#include "application/config/ConfigManager.hpp"
#include "logger/Logger.hpp"
#include <fstream>
#include <cstdlib>

namespace core::config {
    ConfigManager &ConfigManager::getInstance() {
        static ConfigManager instance;
        return instance;
    }

    void ConfigManager::loadDefaults() {
        std::lock_guard<std::mutex> lock(configMutex_);
        config_.clear();
        // Printer check defaults
        config_["printer.check.cache.ttl"] = "5000";
        config_["printer.check.max.retries"] = "3";
        config_["printer.check.default.feed"] = "1000";
        config_["printer.check.default.layer.height"] = "0.2";
        config_["printer.check.timeout.ms"] = "10000";
        config_["printer.check.max.concurrent"] = "5";
        // Queue defaults
        config_["queue.max.commands.in.ram"] = "2000";
        config_["queue.max.completed.jobs"] = "100";
        config_["queue.high.priority.threshold"] = "3";
        config_["queue.enable.disk.paging"] = "true";
        config_["queue.disk.page.path"] = "temp/queue";
        // Serial defaults
        config_["serial.read.timeout.ms"] = "1000";
        config_["serial.write.timeout.ms"] = "5000";
        config_["serial.max.retries"] = "5";
        config_["serial.retry.delay.ms"] = "100";
        config_["serial.enable.keep.alive"] = "true";
        // Performance defaults
        config_["performance.enable.response.cache"] = "true";
        config_["performance.cache.default.ttl"] = "5000";
        config_["performance.max.cache.entries"] = "1000";
        config_["performance.enable.async.data.collection"] = "true";
        config_["performance.background.poll.interval"] = "2000";
        Logger::logInfo("[ConfigManager] Loaded default configuration");
    }

    void ConfigManager::loadFromEnv() {
        std::lock_guard<std::mutex> lock(configMutex_);
        const char *envVars[] = {
            "PRINTER_CHECK_CACHE_TTL", "PRINTER_CHECK_MAX_RETRIES", "PRINTER_CHECK_DEFAULT_FEED",
            "PRINTER_CHECK_DEFAULT_LAYER_HEIGHT", "PRINTER_CHECK_TIMEOUT_MS",
            "QUEUE_MAX_COMMANDS_IN_RAM", "QUEUE_MAX_COMPLETED_JOBS", "QUEUE_ENABLE_DISK_PAGING",
            "SERIAL_READ_TIMEOUT_MS", "SERIAL_WRITE_TIMEOUT_MS", "SERIAL_MAX_RETRIES",
            "PERFORMANCE_ENABLE_CACHE", "PERFORMANCE_CACHE_TTL", "PERFORMANCE_MAX_CACHE_ENTRIES"
        };
        int loaded = 0;
        for (const char *envVar: envVars) {
            const char *value = std::getenv(envVar);
            if (value) {
                std::string key = envVar;
                std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                std::replace(key.begin(), key.end(), '_', '.');
                config_[key] = value;
                loaded++;
            }
        }
        Logger::logInfo("[ConfigManager] Loaded " + std::to_string(loaded) + " settings from environment");
    }

    PrinterCheckConfig ConfigManager::getPrinterCheckConfig() const {
        std::lock_guard<std::mutex> lock(configMutex_);
        PrinterCheckConfig config;
        config.cacheTTL = get<int>("printer.check.cache.ttl", 5000);
        config.maxRetries = get<int>("printer.check.max.retries", 3);
        config.defaultFeed = get<std::string>("printer.check.default.feed", "1000");
        config.defaultLayerHeight = get<std::string>("printer.check.default.layer.height", "0.2");
        config.timeoutMs = get<int>("printer.check.timeout.ms", 10000);
        config.maxConcurrentChecks = get<int>("printer.check.max.concurrent", 5);
        return config;
    }

    QueueConfig ConfigManager::getQueueConfig() const {
        std::lock_guard<std::mutex> lock(configMutex_);
        QueueConfig config;
        config.maxCommandsInRam = get<int>("queue.max.commands.in.ram", 2000);
        config.maxCompletedJobs = get<int>("queue.max.completed.jobs", 100);
        config.highPriorityThreshold = get<int>("queue.high.priority.threshold", 3);
        config.enableDiskPaging = get<bool>("queue.enable.disk.paging", true);
        config.diskPagePath = get<std::string>("queue.disk.page.path", "temp/queue");
        return config;
    }

    SerialConfig ConfigManager::getSerialConfig() const {
        std::lock_guard<std::mutex> lock(configMutex_);
        SerialConfig config;
        config.readTimeoutMs = get<int>("serial.read.timeout.ms", 1000);
        config.writeTimeoutMs = get<int>("serial.write.timeout.ms", 5000);
        config.maxRetries = get<int>("serial.max.retries", 5);
        config.retryDelayMs = get<int>("serial.retry.delay.ms", 100);
        config.enableKeepAlive = get<bool>("serial.enable.keep.alive", true);
        return config;
    }

    PerformanceConfig ConfigManager::getPerformanceConfig() const {
        std::lock_guard<std::mutex> lock(configMutex_);

        PerformanceConfig config;
        config.enableResponseCache = get<bool>("performance.enable.response.cache", true);
        config.cacheDefaultTTL = get<int>("performance.cache.default.ttl", 5000);
        config.maxCacheEntries = get<int>("performance.max.cache.entries", 1000);
        config.enableAsyncDataCollection = get<bool>("performance.enable.async.data.collection", true);
        config.backgroundPollInterval = get<int>("performance.background.poll.interval", 2000);

        return config;
    }
} // namespace core::config
