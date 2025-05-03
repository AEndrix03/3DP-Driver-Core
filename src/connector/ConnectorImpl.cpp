#include "connector/Connector.hpp"
#include "connector/client/WebSocketClient.hpp"
#include "connector/CommandRegistry.hpp"
#include "logger/Logger.hpp"
#include "connector/utils/Time.hpp"
#include "connector/utils/Config.hpp"

#include <nlohmann/json.hpp>
#include <memory>
#include <stdexcept>

namespace connector {

    class ConnectorImpl : public Connector {
    public:
        ConnectorImpl() = default;

        void start() override {
            Logger::logInfo("[Connector] Starting...");

            ws_ = createWebSocketClient(utils::Config::getWebSocketUrl());

            ws_->setOnMessage([this](const std::string &json) {
                Logger::logInfo("[Connector] Received raw: " + json);
                try {
                    auto j = nlohmann::json::parse(json);
                    Command cmd = j.get<Command>();

                    Logger::logInfo("[Connector] Received command: " + cmd.type);
                    receiveCommand(cmd);
                } catch (const std::exception &ex) {
                    Logger::logError("[Connector] JSON parsing failed: " + std::string(ex.what()));
                }
            });

            ws_->connect();
        }

        void stop() override {
            Logger::logInfo("[Connector] Shutting down...");
            if (ws_) ws_->disconnect();
        }

        void sendEvent(const Event &event) override {
            try {
                std::string json = nlohmann::json(event).dump();
                Logger::logInfo("[Connector] Sending event: " + json);
                if (ws_) ws_->send(json);
            } catch (const std::exception &ex) {
                Logger::logError("[Connector] Event send failed: " + std::string(ex.what()));
            }
        }

        void receiveCommand(const Command &command) override {
            try {
                registry_.dispatch(command);
            } catch (const std::exception &ex) {
                Logger::logError("[Connector] Command dispatch failed: " + std::string(ex.what()));
            }
        }

    private:
        std::shared_ptr<WebSocketClient> ws_;
        CommandRegistry registry_;
    };

    std::unique_ptr<Connector> createConnector() {
        return std::make_unique<ConnectorImpl>();
    }

}
