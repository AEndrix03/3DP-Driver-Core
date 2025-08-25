#pragma once

#include <string>

#include "core/printer/PrintState.hpp"

namespace core::print {
    enum class JobState {
        CREATED, // "CRE" - Job creato (non dovrebbe mai apparire nel driver)
        QUEUED, // "QUE" - Job in coda (non dovrebbe mai apparire nel driver)
        RUNNING, // "RUN" - Job in esecuzione
        PAUSED, // "PAU" - Job in pausa
        COMPLETED, // "CMP" - Job completato
        FAILED, // "FAI" - Job fallito
        CANCELLED, // "CNC" - Job cancellato
        PRECHECK,
        HOMING,
        LOADING,
        HEATING,
    };

    /**
     * @brief Convert JobState enum to string code
     */
    inline std::string jobStateToCode(JobState state) {
        switch (state) {
            case JobState::CREATED: return "CRE";
            case JobState::QUEUED: return "QUE";
            case JobState::RUNNING: return "RUN";
            case JobState::PAUSED: return "PAU";
            case JobState::COMPLETED: return "CMP";
            case JobState::FAILED: return "FAI";
            case JobState::CANCELLED: return "CNC";
            case JobState::PRECHECK: return "PRE";
            case JobState::HOMING: return "HOM";
            case JobState::LOADING: return "LOA";
            case JobState::HEATING: return "HEA";
            default: return "UNK";
        }
    }

    /**
     * @brief Convert PrintState enum to string code
     */
    inline std::string printStateToCode(PrintState state) {
        switch (state) {
            case PrintState::Idle: return "IDL";
            case PrintState::Homing: return "HOM";
            case PrintState::Printing: return "PRI";
            case PrintState::Paused: return "PAU";
            case PrintState::Error: return "ERR";
            default: return "UNK";
        }
    }
}
