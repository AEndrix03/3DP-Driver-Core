//
// Created by redeg on 03/05/2025.
//

#pragma once

#include <string>
#include "../external/nlohmann/json/single_include/nlohmann/json.hpp"

namespace connector {

    struct Event {
        std::string type;
        nlohmann::json payload;
    };

    inline void to_json(nlohmann::json &j, const Event &e) {
        j = nlohmann::json{
                {"type",    e.type},
                {"payload", e.payload}
        };
    }

    inline void from_json(const nlohmann::json &j, Event &e) {
        j.at("type").get_to(e.type);
        j.at("payload").get_to(e.payload);
    }

}