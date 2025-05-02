//
// Created by redeg on 01/05/2025.
//

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include "dispatchers/ICommandDispatcher.hpp"
#include "core/DriverInterface.hpp"

namespace translator::gcode {

    class GCodeTranslator {
    public:
        explicit GCodeTranslator(std::shared_ptr<core::DriverInterface> driver);

        void parseLine(const std::string &line);

        void parseLines(const std::vector<std::string> &lines);

        void parseFile(const std::string &filePath);

        void registerDispatcher(std::unique_ptr<ICommandDispatcher> dispatcher);

        std::shared_ptr<core::DriverInterface> getDriver() const;

    private:
        std::shared_ptr<core::DriverInterface> driver_;
        std::vector<std::unique_ptr<ICommandDispatcher>> dispatchers_;

        void dispatchCommand(const std::string &command, const std::map<std::string, double> &params);

        std::pair<std::string, std::map<std::string, double>> parseGCodeLine(const std::string &line);
    };

}
