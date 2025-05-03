#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace connector {

    struct Event {
        std::string id;
        std::string type;
        nlohmann::json payload;
    };

    inline void to_json(nlohmann::json &j, const Event &e) {
        j = nlohmann::json{
                {"id",      e.id},
                {"type",    e.type},
                {"payload", e.payload}
        };
    }

    inline void from_json(const nlohmann::json &j, Event &e) {
        j.at("id").get_to(e.id);
        j.at("type").get_to(e.type);
        j.at("payload").get_to(e.payload);
    }

}
