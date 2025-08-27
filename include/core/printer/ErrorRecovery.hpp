//
// Created by Andrea on 27/08/2025.
//

#pragma once

#include <functional>
#include <chrono>
#include <atomic>
#include <mutex>
#include <thread>
#include <future>
#include <random>

namespace core::recovery {
    enum class CircuitState {
        CLOSED, // Normal operation
        OPEN, // Failing, blocking calls
        HALF_OPEN // Testing if service recovered
    };

    template<typename T>
    struct RetryConfig {
        int maxRetries = 3;
        std::chrono::milliseconds baseDelay{100};
        std::chrono::milliseconds maxDelay{5000};
        double backoffMultiplier = 2.0;
        std::function<bool(const std::exception &)> shouldRetry;

        RetryConfig() {
            shouldRetry = [](const std::exception &) { return true; };
        }
    };

    struct CircuitBreakerConfig {
        int failureThreshold = 5;
        int successThreshold = 2;
        std::chrono::milliseconds timeout{30000};
        std::chrono::milliseconds resetTimeout{60000};
    };

    template<typename T>
    class RetryPolicy {
    public:
        explicit RetryPolicy(RetryConfig<T> config) : config_(std::move(config)) {
        }

        template<typename Func>
        auto execute(Func &&func) -> decltype(func()) {
            using ReturnType = decltype(func());

            std::exception_ptr lastException;
            auto delay = config_.baseDelay;

            for (int attempt = 0; attempt <= config_.maxRetries; ++attempt) {
                try {
                    if constexpr (std::is_void_v<ReturnType>) {
                        func();
                        return;
                    } else {
                        return func();
                    }
                } catch (const std::exception &e) {
                    lastException = std::current_exception();

                    if (attempt == config_.maxRetries || !config_.shouldRetry(e)) {
                        std::rethrow_exception(lastException);
                    }

                    // Exponential backoff with jitter
                    auto jitteredDelay = addJitter(delay);
                    std::this_thread::sleep_for(jitteredDelay);

                    delay = std::min(
                        std::chrono::milliseconds(static_cast<long>(delay.count() * config_.backoffMultiplier)),
                        config_.maxDelay
                    );
                }
            }

            if (lastException) {
                std::rethrow_exception(lastException);
            }

            // Should never reach here
            throw std::runtime_error("Retry policy failed unexpectedly");
        }

    private:
        RetryConfig<T> config_;

        std::chrono::milliseconds addJitter(std::chrono::milliseconds delay) {
            static thread_local std::random_device rd;
            static thread_local std::mt19937 gen(rd());
            std::uniform_real_distribution<> dis(0.5, 1.5);

            return std::chrono::milliseconds(
                static_cast<long>(delay.count() * dis(gen))
            );
        }
    };

    class CircuitBreaker {
    public:
        explicit CircuitBreaker(CircuitBreakerConfig config)
            : config_(config), state_(CircuitState::CLOSED) {
        }

        template<typename Func>
        auto execute(Func &&func) -> decltype(func()) {
            using ReturnType = decltype(func());

            if (state_ == CircuitState::OPEN) {
                if (shouldAttemptReset()) {
                    state_ = CircuitState::HALF_OPEN;
                } else {
                    throw std::runtime_error("Circuit breaker is OPEN");
                }
            }

            try {
                if constexpr (std::is_void_v<ReturnType>) {
                    func();
                    onSuccess();
                } else {
                    auto result = func();
                    onSuccess();
                    return result;
                }
            } catch (...) {
                onFailure();
                throw;
            }
        }

        CircuitState getState() const { return state_; }
        int getFailureCount() const { return failureCount_; }
        int getSuccessCount() const { return successCount_; }

    private:
        CircuitBreakerConfig config_;
        std::atomic<CircuitState> state_;
        std::atomic<int> failureCount_{0};
        std::atomic<int> successCount_{0};
        std::atomic<std::chrono::steady_clock::time_point> lastFailureTime_;
        mutable std::mutex stateMutex_;

        void onSuccess() {
            std::lock_guard<std::mutex> lock(stateMutex_);

            if (state_ == CircuitState::HALF_OPEN) {
                successCount_++;
                if (successCount_ >= config_.successThreshold) {
                    state_ = CircuitState::CLOSED;
                    failureCount_ = 0;
                    successCount_ = 0;
                }
            } else if (state_ == CircuitState::CLOSED) {
                failureCount_ = 0;
            }
        }

        void onFailure() {
            std::lock_guard<std::mutex> lock(stateMutex_);

            failureCount_++;
            lastFailureTime_ = std::chrono::steady_clock::now();

            if (state_ == CircuitState::HALF_OPEN) {
                state_ = CircuitState::OPEN;
                successCount_ = 0;
            } else if (state_ == CircuitState::CLOSED &&
                       failureCount_ >= config_.failureThreshold) {
                state_ = CircuitState::OPEN;
            }
        }

        bool shouldAttemptReset() {
            auto now = std::chrono::steady_clock::now();
            return (now - lastFailureTime_.load()) >= config_.resetTimeout;
        }
    };

    // Combined policy for full resilience
    template<typename T>
    class ResilientExecutor {
    public:
        ResilientExecutor(RetryConfig<T> retryConfig, CircuitBreakerConfig circuitConfig)
            : retryPolicy_(std::move(retryConfig))
              , circuitBreaker_(circuitConfig) {
        }

        template<typename Func>
        auto execute(Func &&func) -> decltype(func()) {
            return circuitBreaker_.execute([this, &func]() {
                return retryPolicy_.execute(std::forward<Func>(func));
            });
        }

        CircuitState getCircuitState() const {
            return circuitBreaker_.getState();
        }

        struct Status {
            CircuitState circuitState;
            int failureCount;
            int successCount;
        };

        Status getStatus() const {
            return {
                circuitBreaker_.getState(),
                circuitBreaker_.getFailureCount(),
                circuitBreaker_.getSuccessCount()
            };
        }

    private:
        RetryPolicy<T> retryPolicy_;
        CircuitBreaker circuitBreaker_;
    };

    // Factory methods for common configurations
    template<typename T>
    RetryConfig<T> createPrinterRetryConfig() {
        RetryConfig<T> config;
        config.maxRetries = 3;
        config.baseDelay = std::chrono::milliseconds(200);
        config.maxDelay = std::chrono::milliseconds(2000);
        config.backoffMultiplier = 1.5;
        config.shouldRetry = [](const std::exception &e) {
            std::string msg = e.what();
            return msg.find("timeout") != std::string::npos ||
                   msg.find("connection") != std::string::npos ||
                   msg.find("busy") != std::string::npos;
        };
        return config;
    }

    inline CircuitBreakerConfig createPrinterCircuitConfig() {
        CircuitBreakerConfig config;
        config.failureThreshold = 5;
        config.successThreshold = 2;
        config.timeout = std::chrono::milliseconds(10000);
        config.resetTimeout = std::chrono::milliseconds(30000);
        return config;
    }
} // namespace core::recovery
