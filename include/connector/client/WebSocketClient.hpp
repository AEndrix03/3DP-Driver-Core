//
// Created by redeg on 03/05/2025.
//

#pragma once

#include <functional>
#include <string>
#include "models/Command.hpp"

namespace connector {

    class WebSocketClient {
    public:
        virtual void connect() = 0;

        virtual void disconnect() = 0;

        virtual void send(const std::string &json) = 0;

        void setOnMessage(std::function<void(const std::string &)> callback) {
            onMessage_ = std::move(callback);
        }

        virtual ~WebSocketClient() = default;

    protected:
        std::function<void(const std::string &)> onMessage_;
    };

    std::shared_ptr<WebSocketClient> createWebSocketClient(const std::string &url);

}
