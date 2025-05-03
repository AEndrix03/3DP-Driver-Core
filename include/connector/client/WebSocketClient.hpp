#pragma once

#include <functional>
#include <memory>
#include <string>

namespace connector {

    class WebSocketClient {
    public:
        virtual void connect() = 0;

        virtual void disconnect() = 0;

        virtual void send(const std::string &json) = 0;

        void setOnMessage(std::function<void(const std::string &)> cb) { onMessage_ = std::move(cb); }

        void setOnOpen(std::function<void()> cb) { onOpen_ = std::move(cb); }

        void setOnClose(std::function<void()> cb) { onClose_ = std::move(cb); }

        virtual ~WebSocketClient() = default;

    protected:
        std::function<void(const std::string &)> onMessage_;
        std::function<void()> onOpen_;
        std::function<void()> onClose_;
    };

    std::shared_ptr<WebSocketClient> createWebSocketClient(const std::string &url);
}
