//
// Created by redeg on 03/05/2025.
//

#pragma once

#include <memory>
#include "models/Command.hpp"
#include "models/Event.hpp"

namespace connector {

    class Connector {
    public:
        virtual void start() = 0;

        virtual void stop() = 0;

        virtual void sendEvent(const Event &event) = 0;

        virtual void receiveCommand(const Command &command) = 0;

        virtual ~Connector() = default;
    };

    std::unique_ptr<Connector> createConnector();
}
