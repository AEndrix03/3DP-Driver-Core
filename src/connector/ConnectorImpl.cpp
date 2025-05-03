#include "connector/Connector.hpp"
#include "connector/client/WebSocketClient.hpp"
#include "connector/bus/RemoteCommandBus.hpp"
#include "logger/Logger.hpp"
#include "connector/utils/Config.hpp"
#include <memory>

namespace connector {

    class ConnectorImpl : public Connector {
    public:
        void start() override {
            Logger::logInfo("[Connector] Starting...");
            ws_ = createWebSocketClient(utils::Config::getWebSocketUrl());

            ws_->setOnMessage([this](const std::string &msg) {
                Logger::logInfo("[Connector] Received raw: " + msg);
                try {
                    Command cmd = nlohmann::json::parse(msg).get<Command>();
                    bus::RemoteCommandBus::dispatch(cmd, *this);
                } catch (const std::exception &e) {
                    Logger::logError("[Connector] JSON error: " + std::string(e.what()));
                }
            });

            ws_->connect();
        }

        void stop() override {
            Logger::logInfo("[Connector] Stopping...");
            if (ws_) ws_->disconnect();
        }

        void sendEvent(const Event &event) override {
            try {
                auto msg = nlohmann::json(event).dump();
                Logger::logInfo("[Connector] Sending event: " + msg);
                if (ws_) ws_->send(msg);
            } catch (const std::exception &e) {
                Logger::logError("[Connector] Send error: " + std::string(e.what()));
            }
        }

        void receiveCommand(const Command & /*cmd*/) override {
            // non usato: usiamo bus::dispatch
        }

    private:
        std::shared_ptr<WebSocketClient> ws_;
    };

    std::unique_ptr<Connector> createConnector() {
        return std::make_unique<ConnectorImpl>();
    }

}
