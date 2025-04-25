//
// Created by redeg on 26/04/2025.
//

#pragma once

#include <unordered_map>
#include <string>

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
        uint16_t nextCommandNumber();

        /**
         * @brief Salva un comando nel buffer storico.
         * @param number Numero del comando.
         * @param commandText Testo completo del comando.
         */
        void storeCommand(uint16_t number, const std::string &commandText);

        /**
         * @brief Recupera il testo di un comando dato il numero.
         * @param number Numero del comando.
         * @return Testo del comando.
         */
        std::string getCommandText(uint16_t number) const;

    private:
        uint16_t currentNumber_;
        std::unordered_map<uint16_t, std::string> history_;
    };

}

