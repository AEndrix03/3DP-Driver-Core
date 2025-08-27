//
// Created by Andrea on 27/08/2025.
//

#include "core/printer/state/StateTracker.hpp"

namespace core::state {
    StateTracker &StateTracker::getInstance() {
        static StateTracker instance;
        return instance;
    }
} // namespace core::state
