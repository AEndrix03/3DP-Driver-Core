//
// Created by redeg on 26/04/2025.
//

#include "core/CommandContext.hpp"

namespace core {

/**
 * @brief Inizializza il contesto con il primo numero comando a 0.
 */
    CommandContext::CommandContext()
            : currentNumber_(0) {}

/**
 * @brief Ottiene il prossimo numero di comando, incrementando il contatore.
 */
    uint16_t CommandContext::nextCommandNumber() {
        return currentNumber_++;
    }

/**
 * @brief Memorizza il testo di un comando associato al suo numero.
 */
    void CommandContext::storeCommand(uint16_t number, const std::string &commandText) {
        history_[number] = commandText;
    }

/**
 * @brief Recupera il testo associato a un numero di comando.
 */
    std::string CommandContext::getCommandText(uint16_t number) const {
        auto it = history_.find(number);
        if (it != history_.end()) {
            return it->second;
        }
        return "";
    }

} // namespace core
