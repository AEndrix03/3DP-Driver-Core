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
    }

    void GCodeTranslator::parseFile(const std::string &filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            std::stringstream ss;
            ss << "[Translator] Errore apertura file: " << filePath;
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
        std::stringstream ss;
        ss << "[Translator] Parsing: " << line;
        Logger::logInfo(ss.str());
        dispatchCommand(command, params);
    }

    std::pair<std::string, std::map<std::string, double>> GCodeTranslator::parseGCodeLine(const std::string &line) {
        std::istringstream stream(line);
        std::string command;
        std::map<std::string, double> params;

        stream >> command;

        std::string token;
        while (stream >> token) {
            if (token.length() < 2) continue;

            char key = token[0];
            double value = std::stod(token.substr(1));
            params[std::string(1, std::toupper(key))] = value;
        }

        return {command, params};
    }

    void GCodeTranslator::dispatchCommand(const std::string &command, const std::map<std::string, double> &params) {
        for (auto &dispatcher: dispatchers_) {
            if (dispatcher->canHandle(command)) {
                if (dispatcher->validate(command, params)) {
                    dispatcher->handle(command, params);
                } else {
                    std::stringstream ss;
                    ss << "[Translator] Comando non valido: " << command;
                    Logger::logWarning(ss.str());

                    throw GCodeTranslatorInvalidCommandException(command);
                }
                return;
            }
        }

        std::stringstream ss;
        ss << "[Translator] Comando non gestito: " << command;
        Logger::logWarning(ss.str());
        throw GCodeTranslatorUnknownCommandException(command);
    }

    void GCodeTranslator::registerDispatcher(std::unique_ptr<ICommandDispatcher> dispatcher) {
        dispatchers_.push_back(std::move(dispatcher));
    }

    std::shared_ptr<core::DriverInterface> GCodeTranslator::getDriver() const {
        return driver_;
    }


}
