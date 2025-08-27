//
// Created by Andrea on 27/08/2025.
//

#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <algorithm>

namespace core::config {
    struct PrinterCheckConfig {
        int cacheTTL = 5000; // ms
        int maxRetries = 3;
        std::string defaultFeed = "1000";
        std::string defaultLayerHeight = "0.2";
        int timeoutMs = 10000;
        int maxConcurrentChecks = 5;
    };

    struct QueueConfig {
        size_t maxCommandsInRam = 2000;
        size_t maxCompletedJobs = 100;
        int highPriorityThreshold = 3;
        bool enableDiskPaging = true;
        std::string diskPagePath = "temp/queue";
    };

    struct SerialConfig {
        int readTimeoutMs = 1000;
        int writeTimeoutMs = 5000;
        int maxRetries = 5;
        int retryDelayMs = 100;
        bool enableKeepAlive = true;
    };

    struct PerformanceConfig {
        bool enableResponseCache = true;
        int cacheDefaultTTL = 5000; // ms
        size_t maxCacheEntries = 1000;
        bool enableAsyncDataCollection = true;
        int backgroundPollInterval = 2000; // ms
    };

    class ConfigManager {
    public:
        static ConfigManager &getInstance();

        // Load configuration
        void loadDefaults();

        void loadFromEnv();

        // Configuration access
        PrinterCheckConfig getPrinterCheckConfig() const;

        QueueConfig getQueueConfig() const;

        SerialConfig getSerialConfig() const;

        PerformanceConfig getPerformanceConfig() const;

        // Generic getters with defaults
        template<typename T>
        T get(const std::string &key, const T &defaultValue) const;

    private:
        ConfigManager() { loadDefaults(); }

        mutable std::mutex configMutex_;
        std::unordered_map<std::string, std::string> config_;
    };

    // Template specializations
    template<>
    inline int ConfigManager::get<int>(const std::string &key, const int &defaultValue) const {
        auto it = config_.find(key);
        if (it == config_.end()) return defaultValue;
        try {
            return std::stoi(it->second);
        } catch (...) {
            return defaultValue;
        }
    }

    template<>
    inline std::string ConfigManager::get<std::string>(const std::string &key, const std::string &defaultValue) const {
        auto it = config_.find(key);
        return (it != config_.end()) ? it->second : defaultValue;
    }

    template<>
    inline bool ConfigManager::get<bool>(const std::string &key, const bool &defaultValue) const {
        auto it = config_.find(key);
        if (it == config_.end()) return defaultValue;
        return it->second == "true" || it->second == "1";
    }

    template<>
    inline double ConfigManager::get<double>(const std::string &key, const double &defaultValue) const {
        auto it = config_.find(key);
        if (it == config_.end()) return defaultValue;
        try {
            return std::stod(it->second);
        } catch (...) {
            return defaultValue;
        }
    }
} // namespace core::config
