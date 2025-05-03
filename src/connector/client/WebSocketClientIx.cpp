#include "connector/client/WebSocketClient.hpp"
#include "logger/Logger.hpp"
#include <ixwebsocket/IXWebSocket.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
#include <random>
#include <chrono>

namespace connector {

    class WebSocketClientIx : public WebSocketClient {
    public:
        explicit WebSocketClientIx(const std::string &url)
                : url_(url),
                  running_(false),
                  connected_(false),
                  backoffSecs_(1),
                  totalReconnects_(0) {}

        void connect() override {
            if (running_) return;
            running_ = true;
            doConnect();
            reconThread_ = std::thread([this]() { reconnectLoop(); });
        }

        void disconnect() override {
            running_ = false;
            {
                std::lock_guard lock(mutex_);
                sendQueue_.clear();
            }
            socket_.stop();
            if (reconThread_.joinable()) reconThread_.join();
            if (hbThread_.joinable()) hbThread_.join();
        }

        void send(const std::string &msg) override {
            std::lock_guard lock(mutex_);
            if (connected_) {
                Logger::logInfo("[WS] Sending: " + msg);
                socket_.send(msg);
            } else {
                Logger::logInfo("[WS] Buffering: " + msg);
                sendQueue_.push_back(msg);
            }
        }

        ~WebSocketClientIx() override { disconnect(); }

    private:
        std::string url_;
        ix::WebSocket socket_;
        std::atomic<bool> running_, connected_;
        std::atomic<int> backoffSecs_, totalReconnects_;
        std::thread reconThread_, hbThread_;
        std::mutex mutex_;
        std::deque<std::string> sendQueue_;

        void doConnect() {
            socket_.setUrl(url_);
            socket_.disableAutomaticReconnection();
            socket_.setPingInterval(0);

            socket_.setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg) {
                if (msg->type == ix::WebSocketMessageType::Open) {
                    onSocketOpen();
                } else if (msg->type == ix::WebSocketMessageType::Close) {
                    onSocketClose();
                } else if (msg->type == ix::WebSocketMessageType::Message) {
                    if (onMessage_) onMessage_(msg->str);
                } else if (msg->type == ix::WebSocketMessageType::Error) {
                    Logger::logError("[WS] Error: " + msg->errorInfo.reason);
                }
            });

            socket_.start();
        }

        void onSocketOpen() {
            {
                std::lock_guard lock(mutex_);
                connected_ = true;
                backoffSecs_ = 1;
                totalReconnects_ = 0;
                for (auto &m: sendQueue_) socket_.send(m);
                sendQueue_.clear();
            }
            Logger::logInfo("[WS] Connected to " + url_);
            if (onOpen_) onOpen_();
            startHeartbeat();
        }

        void onSocketClose() {
            {
                std::lock_guard lock(mutex_);
                connected_ = false;
            }
            Logger::logWarning("[WS] Disconnected");
            if (onClose_) onClose_();
        }

        void reconnectLoop() {
            while (running_) {
                std::this_thread::sleep_for(std::chrono::seconds(backoffSecs_));
                if (!running_) break;
                if (!connected_) {
                    Logger::logInfo("[WS] Reconnect attempt #" + std::to_string(++totalReconnects_));
                    doConnect();
                    // exponential backoff with cap 60s
                    backoffSecs_ = std::min(backoffSecs_ * 2, 60);
                }
            }
        }

        void startHeartbeat() {
            if (hbThread_.joinable()) return;
            hbThread_ = std::thread([this]() {
                while (running_) {
                    std::this_thread::sleep_for(std::chrono::seconds(30));
                    if (connected_) {
                        std::string ping = R"({"type":"ping","payload":{"sentAt":)"
                                           + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count())
                                           + R"(}})";
                        send(ping);
                    }
                }
            });
        }
    };

    std::shared_ptr<WebSocketClient> createWebSocketClient(const std::string &url) {
        return std::make_shared<WebSocketClientIx>(url);
    }

} // namespace connector
