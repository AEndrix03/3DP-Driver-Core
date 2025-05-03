#pragma once

#include <string>

namespace utils {

    class Config {
    public:
        static std::string getWebSocketUrl() {
            return "wss://your-backend-host/ws";
        }
    };

}
