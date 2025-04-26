#pragma once

#include <string>
#include <optional>

namespace core::types {

    enum class ResultCode {
        Success,
        Error,
        Skip,
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

        bool isError() const {
            return code == ResultCode::Error;
        }

        bool isSkip() const {
            return code == ResultCode::Skip;
        }
    };

}