#pragma once

#include <string>

namespace core {

    class Printer {
    public:
        virtual ~Printer() = default;

        virtual void initialize() = 0;

        virtual void shutdown() = 0;
    };

}