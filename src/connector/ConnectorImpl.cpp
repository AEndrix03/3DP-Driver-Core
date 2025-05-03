//
// Created by redeg on 03/05/2025.
//

#include "connector/Connector.hpp"
#include "logger/Logger.hpp"
#include "connector/CommandRegistry.hpp"

namespace connector {

    class ConnectorImpl : public Connector {
    public:
        void start() override {
            Logger::logInfo("Connector started.");
        }

        void stop() override {
            Logger::logInfo("Connector stopped.");
        }

        void sendEvent(const Event &event) override {
            Logger::logInfo("Event sent: " + event.type);
        }

        void receiveCommand(const Command &command) override {
            Logger::logInfo("Command received: " + command.type);
            registry_.dispatch(command);
        }

    private:
        CommandRegistry registry_;
    };

    std::unique_ptr<Connector> createConnector() {
        return std::make_unique<ConnectorImpl>();
    }
}
