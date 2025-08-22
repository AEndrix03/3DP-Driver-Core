//
// Created by redeg on 26/04/2025.
//

#include "core/command/CommandCategoryInterface.hpp"
#include "core/DriverInterface.hpp"

namespace core::command {

    CommandCategoryInterface::CommandCategoryInterface(DriverInterface *driver)
            : driver_(driver) {}

    core::types::Result
    CommandCategoryInterface::sendCommand(char category, int code, const std::vector<std::string> &params) const {
        return driver_->sendCommandInternal(category, code, params);
    }

} // namespace core::printer-command
