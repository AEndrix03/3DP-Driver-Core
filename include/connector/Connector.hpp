#pragma once

#include <memory>

namespace connector {
    class Command;

    class Event;

    class Connector {
    public:
        virtual void start() = 0;

        virtual void stop() = 0;

        virtual void sendEvent(const Event &event) = 0;

        virtual ~Connector() = default;
    };

    std::unique_ptr<Connector> createConnector();
}
