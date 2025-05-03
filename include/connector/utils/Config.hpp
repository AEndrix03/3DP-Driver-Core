//
// Created by redeg on 03/05/2025.
//

#pragma once

#include <string>

namespace utils {

    class Config {
    public:
        static std::string getWebSocketUrl() {
            return "wss://localhost:8080/ws";
        }
    };

}
