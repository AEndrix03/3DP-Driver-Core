//
// Created by redeg on 03/05/2025.
//

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
            socket_.setUrl(url_);
            socket_.setPingInterval(10);
            socket_.disableAutomaticReconnection(); // lo gestiamo noi

            socket_.setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg) {
                if (msg->type == ix::WebSocketMessageType::Message) {
                    logger::info("[WS] Message received");
                    if (onMessage_) onMessage_(msg->str);
                } else if (msg->type == ix::WebSocketMessageType::Open) {
                    logger::info("[WS] Connected to " + url_);
                } else if (msg->type == ix::WebSocketMessageType::Close) {
                    logger::warn("[WS] Connection closed. Attempting reconnect...");
                    scheduleReconnect();
                } else if (msg->type == ix::WebSocketMessageType::Error) {
                    logger::error("[WS] Error: " + msg->errorInfo.reason);
                }
            });

            socket_.start();
            running_ = true;
        }

        void disconnect() override {
            running_ = false;
            socket_.stop();
        }

        void send(const std::string &msg) override {
            logger::debug("[WS] Sending: " + msg);
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
                    logger::info("[WS] Reconnecting attempt " + std::to_string(++retry));
                    connect();  // tenta riconnessione
                    if (socket_.getReadyState() == ix::ReadyState::Open) break;
                }
            }).detach();
        }
    };

    std::shared_ptr<WebSocketClient> createWebSocketClient(const std::string &url) {
        return std::make_shared<WebSocketClientIx>(url);
    }

}
