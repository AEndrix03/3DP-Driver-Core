//
// Created by redeg on 03/05/2025.
//

#pragma once

#include <string>
#include "../external/nlohmann/json/single_include/nlohmann/json.hpp"

namespace connector {

    struct Event {
        std::string type;
        nlohmann::json data;
    };

}