//
// Created by redeg on 01/05/2025.
//

#pragma once

#include <string>
#include <map>

namespace translator::gcode {

    class ICommandDispatcher {
    public:
        virtual ~ICommandDispatcher() = default;

        virtual bool canHandle(const std::string &command) const = 0;

        virtual bool validate(const std::string &command, const std::map<std::string, double> &params) const = 0;

        virtual void handle(const std::string &command, const std::map<std::string, double> &params) = 0;
    };

}
