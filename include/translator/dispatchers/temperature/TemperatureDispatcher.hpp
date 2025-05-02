//
// Created by redeg on 01/05/2025.
//

#pragma once

#include "translator/dispatchers/ICommandDispatcher.hpp"
#include "core/DriverInterface.hpp"
#include <memory>

namespace translator::gcode {

    class TemperatureDispatcher : public ICommandDispatcher {
    public:
        explicit TemperatureDispatcher(std::shared_ptr<core::DriverInterface> driver);

        bool canHandle(const std::string &command) const override;

        bool validate(const std::string &command, const std::map<std::string, double> &params) const override;

        void handle(const std::string &command, const std::map<std::string, double> &params) override;

    private:
        std::shared_ptr<core::DriverInterface> driver_;
    };

} // namespace translator::gcode