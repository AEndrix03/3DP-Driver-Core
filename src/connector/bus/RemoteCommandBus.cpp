#include "connector/bus/RemoteCommandBus.hpp"
#include "connector/handlers/PingCommandHandler.hpp"
#include "logger/Logger.hpp"

namespace connector::bus {

    void RemoteCommandBus::dispatch(const Command &cmd, Connector &connector) {
        if (cmd.type == "ping") {
            handler::PingCommandHandler::handle(cmd, connector);
        } else {
            Logger::logWarning("[RemoteCommandBus] Unknown command: " + cmd.type);
        }
    }

}
