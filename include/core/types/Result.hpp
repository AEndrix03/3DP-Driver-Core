#pragma once
#include <string>
#include <optional>

namespace core::types {

enum class ResultCode {
    Success,
    Error,
    Timeout,
    ChecksumMismatch,
    ResendFailed
};

struct Result {
    ResultCode code;
    std::string message;
    std::optional<int> commandNumber;

    bool isSuccess() const {
        return code == ResultCode::Success;
    }
};

}