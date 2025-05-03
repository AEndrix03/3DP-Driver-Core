//
// Created by redeg on 03/05/2025.
//

#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include "connector/models/Command.hpp"
#include "logger/Logger.hpp"

namespace connector {

    using CommandHandler = std::function<void(const Command &)>;

    class CommandRegistry {
    public:
        void registerHandler(const std::string &type, CommandHandler handler) {
            handlers_[type] = std::move(handler);
        }

        void dispatch(const Command &command) {
            if (auto it = handlers_.find(command.type); it != handlers_.end()) {
                it->second(command);
            } else {
                std::stringstream ss;
                ss << "[Connector] Unknown command: ID " << command.id << " TYPE " << command.type << " PAYLOAD "
                   << command.payload;
                Logger::logWarning(ss.str());
            }
        }

    private:
        std::unordered_map<std::string, CommandHandler> handlers_;
    };
}
