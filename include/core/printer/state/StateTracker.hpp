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
              void updateEPosition(double e) { ePosition_ = e; }
              double getCurrentEPosition() const { return ePosition_; }
              // Feed rate tracking
              void updateFeedRate(double feed) { feedRate_ = feed; }
              double getCurrentFeedRate() const { return feedRate_; }
              // Layer tracking
              void incrementLayer() { currentLayer_++; }
              void setCurrentLayer(int layer) { currentLayer_ = layer; }
              void setLayerHeight(double height) { layerHeight_ = height; }
              int getCurrentLayer() const { return currentLayer_; }
              double getCurrentLayerHeight() const { return layerHeight_; }
              // Fan tracking
              void updateFanSpeed(int speed) { fanSpeed_ = speed; }
              int getCurrentFanSpeed() const { return fanSpeed_; }
              // Target temperature tracking
              void setHotendTargetTemp(double temp) { hotendTargetTemp_ = temp; }
              void setBedTargetTemp(double temp) { bedTargetTemp_ = temp; }
              double getHotendTargetTemp() const { return hotendTargetTemp_; }
              double getBedTargetTemp() const { return bedTargetTemp_; }
              // Actual temperature caching (hotend)
              void updateHotendActualTemp(double temp) {
                     std::lock_guard<std::mutex> lock(tempMutex_);
                     hotendActualTemp_ = temp;
                     hotendTempTime_ = std::chrono::steady_clock::now();
              }

              bool isHotendTempFresh(int maxAgeMs = 3000) const {
                     std::lock_guard<std::mutex> lock(tempMutex_);
                     auto age = std::chrono::steady_clock::now() - hotendTempTime_;
                     return age < std::chrono::milliseconds(maxAgeMs);
              }

              double getCachedHotendTemp() const {
                     std::lock_guard<std::mutex> lock(tempMutex_);
                     return hotendActualTemp_;
              }

              // Actual temperature caching (bed)
              void updateBedActualTemp(double temp) {
                     std::lock_guard<std::mutex> lock(tempMutex_);
                     bedActualTemp_ = temp;
                     bedTempTime_ = std::chrono::steady_clock::now();
              }

              bool isBedTempFresh(int maxAgeMs = 3000) const {
                     std::lock_guard<std::mutex> lock(tempMutex_);
                     auto age = std::chrono::steady_clock::now() - bedTempTime_;
                     return age < std::chrono::milliseconds(maxAgeMs);
              }

              double getCachedBedTemp() const {
                     std::lock_guard<std::mutex> lock(tempMutex_);
                     return bedActualTemp_;
              }

              // Command tracking
              void updateLastCommand(const std::string &cmd) {
                     std::lock_guard<std::mutex> lock(cmdMutex_);
                     lastCommand_ = cmd;
              }

              std::string getLastCommand() const {
                     std::lock_guard<std::mutex> lock(cmdMutex_);
                     return lastCommand_;
              }

              // Statistics
              void incrementCommandCount() { commandCount_++; }
              size_t getCommandCount() const { return commandCount_; }
              // Reset for new job
              void resetForNewJob() {
                     ePosition_ = 0.0;
                     currentLayer_ = 0;
                     commandCount_ = 0;
                     layerHeight_ = 0.2; // Default
                     std::lock_guard<std::mutex> lock(cmdMutex_);
                     lastCommand_.clear();
              }

       private:
              StateTracker() : ePosition_(0.0), feedRate_(1000.0), currentLayer_(0), layerHeight_(0.2), fanSpeed_(0),
                               commandCount_(0),
                               hotendTargetTemp_(0.0), bedTargetTemp_(0.0),
                               hotendActualTemp_(0.0), bedActualTemp_(0.0) {
              }

              // Position and motion state
              std::atomic<double> ePosition_;
              std::atomic<double> feedRate_;
              std::atomic<int> currentLayer_;
              std::atomic<double> layerHeight_;
              std::atomic<int> fanSpeed_;
              std::atomic<size_t> commandCount_;
              // Target temperatures (from M104/M140)
              std::atomic<double> hotendTargetTemp_;
              std::atomic<double> bedTargetTemp_;

              // Actual temperatures (from queries) with cache
              mutable std::mutex tempMutex_;
              double hotendActualTemp_;
              double bedActualTemp_;
              std::chrono::steady_clock::time_point hotendTempTime_;
              std::chrono::steady_clock::time_point bedTempTime_;

              // Command tracking
              mutable std::mutex cmdMutex_;
              std::string lastCommand_;
       };
} // namespace core::state
