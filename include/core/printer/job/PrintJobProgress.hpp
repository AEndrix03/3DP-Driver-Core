//
// Created by Andrea on 24/08/2025.
//

#pragma once
#include <chrono>
#include <string>

#include "PrintJobState.hpp"
#include "core/types/Position.hpp"

namespace core::print {
    struct PrintJobProgress {
        std::string jobId;
        PrintJobState state;
        float percentComplete;
        size_t linesExecuted;
        size_t totalLines;
        std::chrono::seconds elapsed;
        std::chrono::seconds estimated;
        position::Position currentPosition;
        int extruderTemp;
        int bedTemp;
    };
}
