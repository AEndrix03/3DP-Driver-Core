//
// Created by Andrea on 27/08/2025.
//

#pragma once

#include <atomic>
#include <string>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <chrono>
#include <thread>
#include <bits/fs_fwd.h>

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
        void loadFromFile(const std::string &configPath = "config.json");

        void loadFromEnv();

        bool reload();

        // Configuration access
        PrinterCheckConfig getPrinterCheckConfig() const;

        QueueConfig getQueueConfig() const;

        SerialConfig getSerialConfig() const;

        PerformanceConfig getPerformanceConfig() const;

        // Generic getters with defaults
        template<typename T>
        T get(const std::string &key, const T &defaultValue) const;

        // Hot reload support
        void enableHotReload(std::chrono::milliseconds checkInterval = std::chrono::seconds(30));

        void disableHotReload();

        // Change notifications
        using ConfigChangeCallback = std::function<void(const std::string &key, const std::string &oldValue,
                                                        const std::string &newValue)>;

        void registerChangeCallback(const std::string &key, ConfigChangeCallback callback);

        // Validation
        struct ValidationResult {
            bool isValid = true;
            std::vector<std::string> errors;
        };

        ValidationResult validate() const;

    private:
        ConfigManager() = default;

        mutable std::mutex configMutex_;
        std::unordered_map<std::string, std::string> config_;
        std::unordered_map<std::string, ConfigChangeCallback> changeCallbacks_;

        std::string configPath_;
        std::filesystem::file_time_type lastModified_;
        std::thread hotReloadThread_;
        std::atomic<bool> hotReloadEnabled_{false};

        void hotReloadLoop();

        void notifyChange(const std::string &key, const std::string &oldValue, const std::string &newValue);

        // Type converters
        template<typename T>
        T convertValue(const std::string &value) const;

        void setDefaults();

        bool fileChanged() const;
    };

    // Template specializations
    template<>
    inline int ConfigManager::get<int>(const std::string &key, const int &defaultValue) const {
        std::lock_guard<std::mutex> lock(configMutex_);
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
        std::lock_guard<std::mutex> lock(configMutex_);
        auto it = config_.find(key);
        return (it != config_.end()) ? it->second : defaultValue;
    }

    template<>
    inline bool ConfigManager::get<bool>(const std::string &key, const bool &defaultValue) const {
        std::lock_guard<std::mutex> lock(configMutex_);
        auto it = config_.find(key);
        if (it == config_.end()) return defaultValue;
        return it->second == "true" || it->second == "1";
    }

    template<>
    inline double ConfigManager::get<double>(const std::string &key, const double &defaultValue) const {
        std::lock_guard<std::mutex> lock(configMutex_);
        auto it = config_.find(key);
        if (it == config_.end()) return defaultValue;
        try {
            return std::stod(it->second);
        } catch (...) {
            return defaultValue;
        }
    }
} // namespace core::config
