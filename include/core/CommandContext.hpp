//
// Created by redeg on 26/04/2025.
//

#pragma once

#include <unordered_map>
#include <string>
#include <cstdint>

namespace core {

/**
 * @brief Contesto per gestione dei numeri comando e storico comandi inviati.
 */
    class CommandContext {
    public:
        /**
         * @brief Costruttore.
         */
        CommandContext();

        /**
         * @brief Ottiene il prossimo numero di comando disponibile.
         * @return Numero comando incrementale.
         */
        uint32_t nextCommandNumber();

        /**
         * @brief Salva un comando nel buffer storico.
         * @param number Numero del comando.
         * @param commandText Testo completo del comando.
         */
        void storeCommand(uint32_t number, const std::string &commandText);

        /**
         * @brief Rimuove un comando dal buffer storico.
         * @param number Numero del comando
         * @return true se eliminato
         */
        bool removeCommand(uint32_t number);

        /**
         * @brief Recupera il testo di un comando dato il numero.
         * @param number Numero del comando.
         * @return Testo del comando.
         */
        std::string getCommandText(uint32_t number) const;

    private:
        uint32_t currentNumber_;
        std::unordered_map<uint32_t, std::string> history_;
    };

}

