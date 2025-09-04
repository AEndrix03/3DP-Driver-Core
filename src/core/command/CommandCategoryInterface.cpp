//
// Created by redeg on 26/04/2025.
//

#include "core/command/CommandCategoryInterface.hpp"
#include "core/DriverInterface.hpp"
#include "core/exceptions/ResendErrorCommandException.hpp"
#include "core/exceptions/DuplicateErrorCommandException.hpp"

namespace core::command {

    CommandCategoryInterface::CommandCategoryInterface(DriverInterface *driver)
            : driver_(driver) {}

    core::types::Result
    CommandCategoryInterface::sendCommand(char category, int code, const std::vector<std::string> &params) const {
        core::types::Result result = driver_->sendCommandInternal(category, code, params);

        if (result.isResendError())
            throw core::exceptions::ResendErrorCommandException(result.message, result.commandNumber.value_or(0));
        else if (result.isDuplicate())
            throw core::exceptions::DuplicateErrorCommandException(result.message, result.commandNumber.value_or(0));
        else
            return result;
    }

} // namespace core::printer-command
