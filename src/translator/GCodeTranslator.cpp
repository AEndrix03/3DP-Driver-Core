//
// Created by redeg on 01/05/2025.
//

#include "translator/GCodeTranslator.hpp"
#include "translator/exceptions/GCodeTranslatorInvalidCommandException.hpp"
#include "translator/exceptions/GCodeTranslatorUnknownCommandException.hpp"
#include "logger/Logger.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cctype>

namespace translator::gcode {

    GCodeTranslator::GCodeTranslator(std::shared_ptr<core::DriverInterface> driver)
            : driver_(std::move(driver)) {
        Logger::logInfo("[GCodeTranslator] Created with " + std::to_string(dispatchers_.size()) + " dispatchers");
    }

    void GCodeTranslator::parseFile(const std::string &filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            std::stringstream ss;
            ss << "[GCodeTranslator] Error opening file: " << filePath;
            Logger::logError(ss.str());
            return;
        }

        std::string line;
        while (std::getline(file, line)) {
            parseLine(line);
        }
    }

    void GCodeTranslator::parseLines(const std::vector<std::string> &lines) {
        for (const auto &line: lines) {
            parseLine(line);
        }
    }

    void GCodeTranslator::parseLine(const std::string &line) {
        Logger::logInfo("[GCodeTranslator] Parsing line: " + line);

        auto [command, params] = parseGCodeLine(line);

        Logger::logInfo("[GCodeTranslator] Extracted command: " + command);
        if (!params.empty()) {
            std::stringstream paramStr;
            for (const auto &[key, value]: params) {
                paramStr << " " << key << "=" << value;
            }
            Logger::logInfo("[GCodeTranslator] Parameters:" + paramStr.str());
        }

        dispatchCommand(command, params);
    }

    std::pair<std::string, std::map<std::string, double>> GCodeTranslator::parseGCodeLine(const std::string &line) {
        std::istringstream stream(line);
        std::string command;
        std::map<std::string, double> params;

        stream >> command;

        // Convert command to uppercase for consistency
        for (char &c: command) {
            c = std::toupper(c);
        }

        std::string token;
        while (stream >> token) {
            if (token.length() < 2) continue;

            char key = std::toupper(token[0]);
            try {
                double value = std::stod(token.substr(1));
                params[std::string(1, key)] = value;
            } catch (const std::exception &e) {
                Logger::logWarning("[GCodeTranslator] Failed to parse parameter: " + token);
            }
        }

        return {command, params};
    }

    void GCodeTranslator::dispatchCommand(const std::string &command, const std::map<std::string, double> &params) {
        Logger::logInfo("[GCodeTranslator] Dispatching command: " + command +
                        " to " + std::to_string(dispatchers_.size()) + " dispatchers");

        bool handled = false;
        for (auto &dispatcher: dispatchers_) {
            if (dispatcher->canHandle(command)) {
                Logger::logInfo("[GCodeTranslator] Dispatcher found for command: " + command);

                if (dispatcher->validate(command, params)) {
                    Logger::logInfo("[GCodeTranslator] Command validated, handling: " + command);
                    dispatcher->handle(command, params);
                    handled = true;
                    Logger::logInfo("[GCodeTranslator] Command handled successfully: " + command);
                } else {
                    std::stringstream ss;
                    ss << "[GCodeTranslator] Command validation failed: " << command;
                    Logger::logWarning(ss.str());
                    throw GCodeTranslatorInvalidCommandException(command);
                }
                return;
            }
        }

        if (!handled) {
            std::stringstream ss;
            ss << "[GCodeTranslator] No dispatcher found for command: " << command;
            Logger::logWarning(ss.str());
            throw GCodeTranslatorUnknownCommandException(command);
        }
    }

    void GCodeTranslator::registerDispatcher(std::unique_ptr<ICommandDispatcher> dispatcher) {
        dispatchers_.push_back(std::move(dispatcher));
        Logger::logInfo("[GCodeTranslator] Registered dispatcher, total: " + std::to_string(dispatchers_.size()));
    }

    std::shared_ptr<core::DriverInterface> GCodeTranslator::getDriver() const {
        return driver_;
    }

}