#pragma once

#include "connector/models/Command.hpp"
#include "connector/Connector.hpp"

namespace connector::bus {

    class RemoteCommandBus {
    public:
        static void dispatch(const Command &cmd, Connector &connector);
    };

}
