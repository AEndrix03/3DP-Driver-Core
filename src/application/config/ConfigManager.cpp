//
// Created by Andrea on 27/08/2025.
//

#include "application/config/ConfigManager.hpp"
#include "logger/Logger.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <cstdlib>

namespace core::config {
    ConfigManager &ConfigManager::getInstance() {
        static ConfigManager instance;
        return instance;
    }

    void ConfigManager::loadFromFile(const std::string &configPath) {
        std::lock_guard<std::mutex> lock(configMutex_);
        configPath_ = configPath;

        if (!std::filesystem::exists(configPath)) {
            Logger::logWarning("[ConfigManager] Config file not found: " + configPath + ", using defaults");
            setDefaults();
            return;
        }

        try {
            std::ifstream file(configPath);
            nlohmann::json json;
            file >> json;

            // Flatten JSON into key-value pairs
            std::function<void(const nlohmann::json &, const std::string &)> flatten;
            flatten = [&](const nlohmann::json &obj, const std::string &prefix) {
                for (auto it = obj.begin(); it != obj.end(); ++it) {
                    std::string key = prefix.empty() ? it.key() : prefix + "." + it.key();

                    if (it.value().is_object()) {
                        flatten(it.value(), key);
                    } else {
                        config_[key] = it.value().dump();
                        // Remove quotes from string values
                        if (config_[key].front() == '"' && config_[key].back() == '"') {
                            config_[key] = config_[key].substr(1, config_[key].length() - 2);
                        }
                    }
                }
            };

            flatten(json, "");
            lastModified_ = std::filesystem::last_write_time(configPath);

            Logger::logInfo(
                "[ConfigManager] Loaded " + std::to_string(config_.size()) + " settings from " + configPath);
        } catch (const std::exception &e) {
            Logger::logError("[ConfigManager] Failed to load config: " + std::string(e.what()));
            setDefaults();
        }
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
                // Convert ENV_VAR_NAME to dot notation
                std::string key = envVar;
                std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                std::replace(key.begin(), key.end(), '_', '.');

                config_[key] = value;
                loaded++;
            }
        }

        Logger::logInfo("[ConfigManager] Loaded " + std::to_string(loaded) + " settings from environment");
    }

    bool ConfigManager::reload() {
        if (configPath_.empty()) return false;

        try {
            auto oldConfig = config_;
            loadFromFile(configPath_);

            // Notify changes
            for (const auto &[key, newValue]: config_) {
                auto it = oldConfig.find(key);
                if (it == oldConfig.end() || it->second != newValue) {
                    std::string oldValue = (it != oldConfig.end()) ? it->second : "";
                    notifyChange(key, oldValue, newValue);
                }
            }

            return true;
        } catch (const std::exception &e) {
            Logger::logError("[ConfigManager] Reload failed: " + std::string(e.what()));
            return false;
        }
    }

    PrinterCheckConfig ConfigManager::getPrinterCheckConfig() const {
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
        QueueConfig config;
        config.maxCommandsInRam = get<int>("queue.max.commands.in.ram", 2000);
        config.maxCompletedJobs = get<int>("queue.max.completed.jobs", 100);
        config.highPriorityThreshold = get<int>("queue.high.priority.threshold", 3);
        config.enableDiskPaging = get<bool>("queue.enable.disk.paging", true);
        config.diskPagePath = get<std::string>("queue.disk.page.path", "temp/queue");
        return config;
    }

    SerialConfig ConfigManager::getSerialConfig() const {
        SerialConfig config;
        config.readTimeoutMs = get<int>("serial.read.timeout.ms", 1000);
        config.writeTimeoutMs = get<int>("serial.write.timeout.ms", 5000);
        config.maxRetries = get<int>("serial.max.retries", 5);
        config.retryDelayMs = get<int>("serial.retry.delay.ms", 100);
        config.enableKeepAlive = get<bool>("serial.enable.keep.alive", true);
        return config;
    }

    PerformanceConfig ConfigManager::getPerformanceConfig() const {
        PerformanceConfig config;
        config.enableResponseCache = get<bool>("performance.enable.response.cache", true);
        config.cacheDefaultTTL = get<int>("performance.cache.default.ttl", 5000);
        config.maxCacheEntries = get<int>("performance.max.cache.entries", 1000);
        config.enableAsyncDataCollection = get<bool>("performance.enable.async.data.collection", true);
        config.backgroundPollInterval = get<int>("performance.background.poll.interval", 2000);
        return config;
    }

    void ConfigManager::enableHotReload(std::chrono::milliseconds checkInterval) {
        if (hotReloadEnabled_ || configPath_.empty()) return;

        hotReloadEnabled_ = true;
        hotReloadThread_ = std::thread([this]() {
            hotReloadLoop();
        });

        Logger::logInfo("[ConfigManager] Hot reload enabled");
    }

    void ConfigManager::disableHotReload() {
        if (!hotReloadEnabled_) return;

        hotReloadEnabled_ = false;
        if (hotReloadThread_.joinable()) {
            hotReloadThread_.join();
        }

        Logger::logInfo("[ConfigManager] Hot reload disabled");
    }

    void ConfigManager::registerChangeCallback(const std::string &key, ConfigChangeCallback callback) {
        std::lock_guard<std::mutex> lock(configMutex_);
        changeCallbacks_[key] = std::move(callback);
    }

    ConfigManager::ValidationResult ConfigManager::validate() const {
        ValidationResult result;

        // Validate printer check config
        if (get<int>("printer.check.cache.ttl", -1) < 0) {
            result.errors.push_back("printer.check.cache.ttl must be >= 0");
        }

        if (get<int>("printer.check.max.retries", -1) < 1) {
            result.errors.push_back("printer.check.max.retries must be >= 1");
        }

        // Validate queue config
        if (get<int>("queue.max.commands.in.ram", -1) < 100) {
            result.errors.push_back("queue.max.commands.in.ram must be >= 100");
        }

        // Validate serial config
        if (get<int>("serial.read.timeout.ms", -1) < 100) {
            result.errors.push_back("serial.read.timeout.ms must be >= 100");
        }

        result.isValid = result.errors.empty();
        return result;
    }

    void ConfigManager::setDefaults() {
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

    void ConfigManager::hotReloadLoop() {
        while (hotReloadEnabled_) {
            std::this_thread::sleep_for(std::chrono::seconds(30));

            if (fileChanged()) {
                Logger::logInfo("[ConfigManager] Config file changed, reloading...");
                reload();
            }
        }
    }

    bool ConfigManager::fileChanged() const {
        if (configPath_.empty() || !std::filesystem::exists(configPath_)) {
            return false;
        }

        auto currentModified = std::filesystem::last_write_time(configPath_);
        return currentModified > lastModified_;
    }

    void ConfigManager::notifyChange(const std::string &key, const std::string &oldValue, const std::string &newValue) {
        auto it = changeCallbacks_.find(key);
        if (it != changeCallbacks_.end()) {
            try {
                it->second(key, oldValue, newValue);
            } catch (const std::exception &e) {
                Logger::logError("[ConfigManager] Change callback failed for " + key + ": " + e.what());
            }
        }
    }
} // namespace core::config
