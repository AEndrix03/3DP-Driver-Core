#include "connector/handlers/PingCommandHandler.hpp"
#include "connector/utils/Time.hpp"
#include "logger/Logger.hpp"

namespace connector::handler {

    void PingCommandHandler::handle(const Command &cmd, Connector &connector) {
        Logger::logInfo("[Ping] Handling 'ping'");

        long long now = utils::currentTimeMillis();
        long long latency = -1;

        if (cmd.payload.contains("sentAt")) {
            try {
                latency = now - cmd.payload["sentAt"].get<long long>();
            } catch (...) {
                Logger::logWarning("[Ping] Invalid 'sentAt'");
            }
        }

        Event pong{.id=cmd.id, .type="pong", .payload={
                {"receivedAt", now},
                {"latency",    latency}
        }};

        connector.sendEvent(pong);
    }

}
