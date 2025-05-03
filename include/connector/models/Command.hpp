#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace connector {

    struct Command {
        std::string id;
        std::string type;
        nlohmann::json payload;
    };

    inline void to_json(nlohmann::json &j, const Command &c) {
        j = nlohmann::json{
                {"id",      c.id},
                {"type",    c.type},
                {"payload", c.payload}
        };
    }

    inline void from_json(const nlohmann::json &j, Command &c) {
        j.at("id").get_to(c.id);
        j.at("type").get_to(c.type);
        j.at("payload").get_to(c.payload);
    }

}
