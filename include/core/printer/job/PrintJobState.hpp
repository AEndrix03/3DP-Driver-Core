#pragma once

namespace core::print {
    enum class PrintJobState {
        Idle,
        Loading, // Loading G-code file
        PreCheck, // Running safety checks
        Heating, // Waiting for temperatures
        Ready, // Ready to start
        Printing, // Active printing
        Paused, // Paused by user
        Finishing, // Final moves/cooling
        Completed, // Job completed successfully
        Error, // Error state
        Cancelled // Cancelled by user
    };
}
