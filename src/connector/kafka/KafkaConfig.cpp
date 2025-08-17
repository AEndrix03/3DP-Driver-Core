#include "connector/kafka/KafkaConfig.hpp"
#include "logger/Logger.hpp"
#include <cstdlib>
#include <string>
#include <regex>
#include <fstream>
#include <iostream>

namespace connector::kafka {

    std::string KafkaConfig::resolvePlaceholder(const std::string &value) {
        // Regex per ${VAR_NAME:default_value}
        std::regex placeholder_regex(R"(\$\{([^}:]+):([^}]*)\})");
        std::string result = value;

        std::smatch matches;
        while (std::regex_search(result, matches, placeholder_regex)) {
            std::string varName = matches[1].str();
            std::string defaultValue = matches[2].str();
            std::string fullMatch = matches[0].str();

            // Cerca la variabile d'ambiente
            const char *envValue = std::getenv(varName.c_str());
            std::string replacement = envValue ? envValue : defaultValue;

            // Log per debug
            if (envValue) {
                Logger::logInfo("[KafkaConfig] Resolved " + varName + " = " + replacement);
            } else {
                Logger::logInfo("[KafkaConfig] Using default for " + varName + " = " + replacement);
            }

            // Sostituisce il placeholder con il valore risolto
            size_t pos = result.find(fullMatch);
            if (pos != std::string::npos) {
                result.replace(pos, fullMatch.length(), replacement);
            }
        }

        return result;
    }

    void KafkaConfig::loadEnvFile(const std::string &envFilePath) {
        std::ifstream envFile(envFilePath);
        if (!envFile.is_open()) {
            Logger::logInfo("[KafkaConfig] No .env file found at: " + envFilePath + " (using system environment only)");
            return;
        }

        Logger::logInfo("[KafkaConfig] Loading .env file: " + envFilePath);

        std::string line;
        int loadedVars = 0;

        while (std::getline(envFile, line)) {
            // Rimuovi spazi all'inizio e alla fine
            line.erase(0, line.find_first_not_of(" \t"));
            line.erase(line.find_last_not_of(" \t") + 1);

            // Salta righe vuote e commenti
            if (line.empty() || line[0] == '#') continue;

            // Parse KEY=VALUE
            size_t pos = line.find('=');
            if (pos == std::string::npos) continue;

            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);

            // Rimuovi spazi
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            // Rimuovi virgolette se presenti
            if (value.size() >= 2 &&
                ((value.front() == '"' && value.back() == '"') ||
                 (value.front() == '\'' && value.back() == '\''))) {
                value = value.substr(1, value.size() - 2);
            }

            // Setta la variabile d'ambiente (non sovrascrivere se già esistente)
            if (std::getenv(key.c_str()) == nullptr) {
#ifdef _WIN32
                _putenv_s(key.c_str(), value.c_str());
#else
                setenv(key.c_str(), value.c_str(), 0);
#endif
                Logger::logInfo("[KafkaConfig] Loaded from .env: " + key + " = " + value);
                loadedVars++;
            } else {
                Logger::logInfo("[KafkaConfig] Skipped (already set): " + key);
            }
        }

        envFile.close();
        Logger::logInfo("[KafkaConfig] Loaded " + std::to_string(loadedVars) + " variables from .env file");
    }

    void KafkaConfig::resolveFromEnvironment() {
        // Prima carica il file .env
        loadEnvFile(".env");

        // Poi risolve tutti i placeholder
        brokers = resolvePlaceholder(brokers);
        clientId = resolvePlaceholder(clientId);
        consumerGroupId = resolvePlaceholder(consumerGroupId);
        autoOffsetReset = resolvePlaceholder(autoOffsetReset);
        compressionType = resolvePlaceholder(compressionType);
        sslCaLocation = resolvePlaceholder(sslCaLocation);
        sslCertLocation = resolvePlaceholder(sslCertLocation);
        sslKeyLocation = resolvePlaceholder(sslKeyLocation);
        saslMechanism = resolvePlaceholder(saslMechanism);
        saslUsername = resolvePlaceholder(saslUsername);
        saslPassword = resolvePlaceholder(saslPassword);
        driverId = resolvePlaceholder(driverId);
        location = resolvePlaceholder(location);
        serialPort = resolvePlaceholder(serialPort);

        Logger::logInfo("[KafkaConfig] All placeholders resolved");
    }

    void KafkaConfig::printConfig() const {
        Logger::logInfo("[KafkaConfig] Final configuration:");
        Logger::logInfo("  Brokers: " + brokers);
        Logger::logInfo("  Client ID: " + clientId);
        Logger::logInfo("  Consumer Group: " + consumerGroupId);
        Logger::logInfo("  Driver ID: " + driverId);
        Logger::logInfo("  Location: " + location);
        Logger::logInfo("  Serial Port: " + serialPort + " @ " + std::to_string(serialBaudrate) + " baud");
        Logger::logInfo("  SSL Enabled: " + std::string(enableSsl ? "true" : "false"));

        if (!saslMechanism.empty()) {
            Logger::logInfo("  SASL Mechanism: " + saslMechanism);
        }
    }

}