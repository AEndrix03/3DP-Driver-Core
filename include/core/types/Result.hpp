#pragma once

#include <string>
#include <optional>
#include <vector>

namespace core::types {

    enum class ResultCode {
        Success,
        Error,
        Skip,
        Busy,
        Timeout,
        ChecksumMismatch,
        ResendFailed
    };

    struct Result {
        ResultCode code;
        std::string message;
        std::optional<int> commandNumber;
        std::vector<std::string> body;

        bool isSuccess() const {
            return code == ResultCode::Success;
        }

        bool isError() const {
            return code == ResultCode::Error;
        }

        bool isSkip() const {
            return code == ResultCode::Skip;
        }

        bool isBusy() const {
            return code == ResultCode::Busy;
        }
    };

}