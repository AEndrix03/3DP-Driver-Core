#pragma once

#include <string>
#include <optional>
#include <vector>
#include <cstdint>

namespace core::types {

    enum class ResultCode {
        Success,
        Error,
        Skip,
        Busy,
        Timeout,
        ChecksumMismatch,
        BufferOverflow,
        Resend,
        ResendError,
        Duplicate
    };

    struct Result {
        ResultCode code;
        std::string message;
        std::optional<uint32_t> commandNumber;
        std::vector<std::string> body;

        inline bool isSuccess() const {
            return code == ResultCode::Success;
        }

        inline bool isError() const {
            return code == ResultCode::Error;
        }

        inline bool isSkip() const {
            return code == ResultCode::Skip;
        }

        inline bool isBusy() const {
            return code == ResultCode::Busy;
        }

        inline bool isResend() const {
            return code == ResultCode::Resend;
        }

        inline bool isResendError() const {
            return code == ResultCode::ResendError;
        }

        inline bool isDuplicate() const {
            return code == ResultCode::Duplicate;
        }

        inline bool isChecksumMismatch() const {
            return code == ResultCode::ChecksumMismatch;
        }

        inline bool isBufferOverflow() const {
            return code == ResultCode::BufferOverflow;
        }

        static inline Result success(const std::string &msg = "Success") {
            return {ResultCode::Success, msg, std::nullopt, {}};
        }

        static inline Result error(const std::string &msg = "Error") {
            return {ResultCode::Error, msg, std::nullopt, {}};
        }

        static inline Result duplicate(const uint32_t cmdNum) {
            return {ResultCode::Duplicate, "DUPLICATE ERROR", cmdNum, {}};
        }

        static inline Result resendError(const uint32_t cmdNum) {
            return {ResultCode::ResendError, "RESEND ERROR - command not in history", cmdNum, {}};
        }
    };

}