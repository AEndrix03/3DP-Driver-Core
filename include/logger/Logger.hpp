//
// Created by redeg on 26/04/2025.
//

#pragma once

#include <string>
#include <fstream>

/**
 * @brief Logger di base per driver, salva su file e stampa su console.
 */
class Logger {
public:
    /**
     * @brief Inizializza il logger con il file di output.
     */
    static void init();

    /**
     * @brief Logga un messaggio informativo.
     */
    static void logInfo(const std::string &message);

    /**
     * @brief Logga un warning.
     */
    static void logWarning(const std::string &message);

    /**
     * @brief Logga un errore.
     */
    static void logError(const std::string &message);

private:
    static std::ofstream logFile_;

    static void log(const std::string &level, const std::string &message);

    static std::string currentTimestamp();

    static std::string generateLogFilename();
};