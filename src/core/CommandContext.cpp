//
// Created by redeg on 26/04/2025.
//

#include "core/CommandContext.hpp"
#include "logger/Logger.hpp"
#include <algorithm>

namespace core {

    CommandContext::CommandContext()
            : currentNumber_(1) {
        // Reserve space for command history
        history_.reserve(100);
    }

    uint32_t CommandContext::nextCommandNumber() {
        return currentNumber_++;
    }

    void CommandContext::storeCommand(uint32_t number, const std::string &commandText) {
        // Keep only last 100 commands to prevent memory issues
        if (history_.size() >= 100) {
            // Remove oldest commands (those with lowest numbers)
            auto minIt = std::min_element(history_.begin(), history_.end(),
                                          [](const auto &a, const auto &b) {
                                              return a.first < b.first;
                                          });
            if (minIt != history_.end()) {
                history_.erase(minIt);
            }
        }

        history_[number] = commandText;
        Logger::logInfo("[CommandContext] Stored command N" + std::to_string(number) +
                        " (history size: " + std::to_string(history_.size()) + ")");
    }

    bool CommandContext::removeCommand(uint32_t number) {
        size_t removed = history_.erase(number);
        if (removed > 0) {
            Logger::logInfo("[CommandContext] Removed command N" + std::to_string(number) +
                            " from history (history size: " + std::to_string(history_.size()) + ")");
            return true;
        }

        Logger::logWarning("[CommandContext] No command found with N" + std::to_string(number) + "signature");
        return false;
    }

    std::string CommandContext::getCommandText(uint32_t number) const {
        auto it = history_.find(number);
        if (it != history_.end()) {
            Logger::logInfo("[CommandContext] Retrieved command N" + std::to_string(number));
            return it->second;
        }
        Logger::logWarning("[CommandContext] Command N" + std::to_string(number) + " not found in history");
        return "";
    }

} // namespace core