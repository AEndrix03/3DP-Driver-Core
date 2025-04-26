#pragma once

namespace core {

enum class PrintState {
    Idle,
    Running,
    Paused,
    Completed,
    Error
};

}