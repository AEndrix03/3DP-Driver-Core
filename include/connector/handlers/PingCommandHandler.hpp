#pragma once

#include "connector/models/Command.hpp"
#include "connector/models/Event.hpp"
#include "connector/Connector.hpp"

namespace connector::handler {

    class PingCommandHandler {
    public:
        static void handle(const Command &cmd, Connector &connector);
    };

}
