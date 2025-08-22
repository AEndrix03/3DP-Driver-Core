#include "application/controllers/ApplicationController.hpp"
#include "logger/Logger.hpp"
#include <csignal>
#include <atomic>
#include <condition_variable>
#include <mutex>

// Global shutdown mechanism
std::atomic<bool> running{true};
std::condition_variable shutdownCondition;
std::mutex shutdownMutex;
ApplicationController *appController = nullptr;

void handleSignal(int signal) {
    Logger::logInfo("Received shutdown signal: " + std::to_string(signal));
    running = false;
    shutdownCondition.notify_all();
}

void waitForShutdownSignal() {
    std::unique_lock<std::mutex> lock(shutdownMutex);
    shutdownCondition.wait(lock, [] { return !running.load(); });
}

int main() {
    try {
        // Initialize logger and signal handling
        Logger::init();
        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);

        // Create and initialize application
        ApplicationController app;
        appController = &app;

        if (!app.initialize()) {
            Logger::logError("Application initialization failed");
            return 1;
        }

        // Wait for shutdown signal
        waitForShutdownSignal();

        // Cleanup
        app.shutdown();
    } catch (const std::exception &ex) {
        Logger::logError("Fatal error: " + std::string(ex.what()));
        return 1;
    }

    return 0;
}
