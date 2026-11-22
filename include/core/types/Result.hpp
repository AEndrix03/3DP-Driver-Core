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
        Duplicate,
        SyncLost,   // il firmware si e' riavviato o ha perso la sequenza: stato macchina non piu' attendibile
        Rejected    // comando valido ma rifiutato dal firmware (E06): dettaglio in message
    };

    struct Result {
        ResultCode code;
        std::string message;
        std::optional<uint32_t> commandNumber;
        std::vector<std::string> body;

        /// "D" della risposta OK: ultimo comando la cui esecuzione e' terminata sul firmware.
        /// I movimenti rispondono OK appena sono in coda, quindi puo' essere minore di commandNumber.
        std::optional<uint32_t> completedNumber = std::nullopt;

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

        inline bool isTimeout() const {
            return code == ResultCode::Timeout;
        }

        inline bool isSyncLost() const {
            return code == ResultCode::SyncLost;
        }

        inline bool isRejected() const {
            return code == ResultCode::Rejected;
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