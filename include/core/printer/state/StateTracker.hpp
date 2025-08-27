//
// Created by Andrea on 27/08/2025.
//

#pragma once

#include <atomic>
#include <mutex>
#include <chrono>
#include <string>

namespace core::state {
    class StateTracker {
    public:
        static StateTracker &getInstance();

        // Position tracking
        void updateEPosition(double e) {
            ePosition_ = e;
            lastUpdate_ = now();
        }

        double getCurrentEPosition() const { return ePosition_; }

        // Feed rate tracking
        void updateFeedRate(double feed) {
            feedRate_ = feed;
            lastUpdate_ = now();
        }

        double getCurrentFeedRate() const { return feedRate_; }

        // Layer tracking
        void incrementLayer() {
            currentLayer_++;
            lastUpdate_ = now();
        }

        void setCurrentLayer(int layer) {
            currentLayer_ = layer;
            lastUpdate_ = now();
        }

        void setLayerHeight(double height) {
            layerHeight_ = height;
            lastUpdate_ = now();
        }

        int getCurrentLayer() const { return currentLayer_; }
        double getCurrentLayerHeight() const { return layerHeight_; }

        // Fan tracking
        void updateFanSpeed(int speed) {
            fanSpeed_ = speed;
            lastUpdate_ = now();
        }

        int getCurrentFanSpeed() const { return fanSpeed_; }

        // Temperature tracking (cached)
        void updateHotendTemp(double temp) {
            std::lock_guard<std::mutex> lock(tempMutex_);
            hotendTemp_ = temp;
            hotendTempTime_ = now();
        }

        void updateBedTemp(double temp) {
            std::lock_guard<std::mutex> lock(tempMutex_);
            bedTemp_ = temp;
            bedTempTime_ = now();
        }

        bool isHotendTempFresh(int maxAgeMs = 5000) const {
            std::lock_guard<std::mutex> lock(tempMutex_);
            return (now() - hotendTempTime_) < std::chrono::milliseconds(maxAgeMs);
        }

        double getCachedHotendTemp() const {
            std::lock_guard<std::mutex> lock(tempMutex_);
            return hotendTemp_;
        }

        double getCachedBedTemp() const {
            std::lock_guard<std::mutex> lock(tempMutex_);
            return bedTemp_;
        }

        // Command tracking
        void updateLastCommand(const std::string &cmd) {
            std::lock_guard<std::mutex> lock(cmdMutex_);
            lastCommand_ = cmd;
            lastUpdate_ = now();
        }

        std::string getLastCommand() const {
            std::lock_guard<std::mutex> lock(cmdMutex_);
            return lastCommand_;
        }

        // Statistics
        void incrementCommandCount() { commandCount_++; }
        size_t getCommandCount() const { return commandCount_; }

        std::chrono::steady_clock::time_point getLastUpdateTime() const { return lastUpdate_; }

        // Reset for new job
        void resetForNewJob() {
            ePosition_ = 0.0;
            currentLayer_ = 0;
            commandCount_ = 0;
            layerHeight_ = 0.2; // Default
            lastUpdate_ = now();

            std::lock_guard<std::mutex> lock(cmdMutex_);
            lastCommand_.clear();
        }

    private:
        StateTracker() : ePosition_(0.0), feedRate_(1000.0), currentLayer_(0),
                         layerHeight_(0.2), fanSpeed_(0), commandCount_(0),
                         hotendTemp_(0.0), bedTemp_(0.0) {
        }

        std::atomic<double> ePosition_;
        std::atomic<double> feedRate_;
        std::atomic<int> currentLayer_;
        std::atomic<double> layerHeight_;
        std::atomic<int> fanSpeed_;
        std::atomic<size_t> commandCount_;
        std::atomic<std::chrono::steady_clock::time_point> lastUpdate_;

        // Temperature cache with timestamps
        mutable std::mutex tempMutex_;
        double hotendTemp_;
        double bedTemp_;
        std::chrono::steady_clock::time_point hotendTempTime_;
        std::chrono::steady_clock::time_point bedTempTime_;

        // Command tracking
        mutable std::mutex cmdMutex_;
        std::string lastCommand_;

        static std::chrono::steady_clock::time_point now() {
            return std::chrono::steady_clock::now();
        }
    };
} // namespace core::state
