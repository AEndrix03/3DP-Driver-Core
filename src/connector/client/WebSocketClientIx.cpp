#include "connector/client/WebSocketClient.hpp"
#include "logger/Logger.hpp"
#include "connector/utils/Time.hpp"
#include <ixwebsocket/IXWebSocket.h>
#include <thread>
#include <atomic>

namespace connector {

    class WebSocketClientIx : public WebSocketClient {
    public:
        explicit WebSocketClientIx(const std::string &url)
                : url_(url), running_(false) {}

        void connect() override {
            if (running_) return;

            running_ = true;
            socket_.setUrl(url_);
            socket_.setPingInterval(10);
            socket_.disableAutomaticReconnection();

            socket_.setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg) {
                switch (msg->type) {
                    case ix::WebSocketMessageType::Message:
                        Logger::logInfo("[WS] Message received");
                        if (onMessage_) onMessage_(msg->str);
                        break;
                    case ix::WebSocketMessageType::Open:
                        Logger::logInfo("[WS] Connected to " + url_);
                        break;
                    case ix::WebSocketMessageType::Close:
                        Logger::logWarning("[WS] Connection closed. Scheduling reconnect...");
                        scheduleReconnect();
                        break;
                    case ix::WebSocketMessageType::Error:
                        Logger::logError("[WS] Error: " + msg->errorInfo.reason);
                        break;
                    default:
                        Logger::logWarning("[WS] Unknown message type");
                        break;
                }
            });

            socket_.start();
        }

        void disconnect() override {
            running_ = false;
            socket_.stop();
        }

        void send(const std::string &msg) override {
            Logger::logInfo("[WS] Sending: " + msg);
            socket_.send(msg);
        }

        ~WebSocketClientIx() override {
            disconnect();
        }

    private:
        std::string url_;
        ix::WebSocket socket_;
        std::atomic<bool> running_;

        void scheduleReconnect() {
            std::thread([this]() {
                int retry = 0;
                while (running_) {
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    if (socket_.getReadyState() != ix::ReadyState::Open) {
                        Logger::logInfo("[WS] Reconnecting attempt " + std::to_string(++retry));
                        connect();
                    } else {
                        break;
                    }
                }
            }).detach();
        }
    };

    std::shared_ptr<WebSocketClient> createWebSocketClient(const std::string &url) {
        return std::make_shared<WebSocketClientIx>(url);
    }

}
