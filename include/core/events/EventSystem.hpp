//
// Created by redeg on 31/08/2025.
//

#pragma once

#include <functional>
#include <vector>
#include <memory>
#include <mutex>
#include <string>

namespace core::events {

    enum class EventType {
        QUEUE_STARTED,
        QUEUE_STOPPED,
        QUEUE_STALLED,
        COMMAND_EXECUTED,
        KAFKA_MESSAGE_RECEIVED,
        HARDWARE_ERROR
    };

    struct Event {
        EventType type;
        std::string source;
        std::string message;
        std::chrono::steady_clock::time_point timestamp;

        Event(EventType t, std::string src, std::string msg = "")
                : type(t), source(std::move(src)), message(std::move(msg)),
                  timestamp(std::chrono::steady_clock::now()) {}
    };

    class IEventObserver {
    public:
        virtual ~IEventObserver() = default;

        virtual void onEvent(const Event &event) = 0;
    };

    class EventBus {
    private:
        mutable std::mutex observersMutex_;
        std::vector<std::weak_ptr<IEventObserver>> observers_;

    public:
        static EventBus &getInstance() {
            static EventBus instance;
            return instance;
        }

        void subscribe(std::shared_ptr<IEventObserver> observer) {
            std::lock_guard<std::mutex> lock(observersMutex_);
            observers_.push_back(observer);
        }

        void publish(const Event &event) {
            std::lock_guard<std::mutex> lock(observersMutex_);

            // Clean up expired observers and notify active ones
            auto it = observers_.begin();
            while (it != observers_.end()) {
                if (auto observer = it->lock()) {
                    observer->onEvent(event);
                    ++it;
                } else {
                    it = observers_.erase(it);
                }
            }
        }
    };

} // namespace core::events