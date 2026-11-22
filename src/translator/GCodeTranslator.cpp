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
        auto [command, params] = parseGCodeLine(line);
        if (command.empty()) return; // riga di solo commento

        dispatchCommand(command, params);
    }

    std::pair<std::string, std::map<std::string, double>> GCodeTranslator::parseGCodeLine(const std::string &rawLine) {
        // I commenti non sono parametri: "G1 X1 ; E3D nozzle" non deve estrudere 3 mm
        std::string line = rawLine.substr(0, rawLine.find(';'));
        for (size_t open = line.find('('); open != std::string::npos; open = line.find('(')) {
            const size_t close = line.find(')', open);
            line.erase(open, close == std::string::npos ? std::string::npos : close - open + 1);
        }

        std::istringstream stream(line);
        std::string command;
        std::map<std::string, double> params;

        stream >> command;

        for (char &c: command) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        // "G01" e "G1" sono lo stesso comando
        while (command.size() > 2 && command[1] == '0' && std::isdigit(static_cast<unsigned char>(command[2]))) {
            command.erase(1, 1);
        }

        std::string token;
        while (stream >> token) {
            if (token.length() < 2) continue;

            const char key = static_cast<char>(std::toupper(static_cast<unsigned char>(token[0])));
            if (!std::isalpha(static_cast<unsigned char>(key))) continue;
            try {
                size_t used = 0;
                const double value = std::stod(token.substr(1), &used);
                if (used != token.size() - 1) throw std::invalid_argument(token);
                params[std::string(1, key)] = value;
            } catch (const std::exception &) {
                Logger::logWarning("[GCodeTranslator] Failed to parse parameter: " + token);
            }
        }

        return {command, params};
    }

    void GCodeTranslator::dispatchCommand(const std::string &command, const std::map<std::string, double> &params) {
        bool handled = false;
        for (auto &dispatcher: dispatchers_) {
            if (dispatcher->canHandle(command)) {
                if (dispatcher->validate(command, params)) {
                    dispatcher->handle(command, params);
                    handled = true;
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