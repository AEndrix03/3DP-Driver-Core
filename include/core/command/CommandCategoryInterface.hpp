//
// Created by redeg on 26/04/2025.
//

#pragma once

#include "core/types/Result.hpp"
#include <vector>
#include <string>

namespace core {

    class DriverInterface; // Forward declaration

    namespace command {

/**
 * @brief Interfaccia base per ogni categoria di comandi.
 */
        class CommandCategoryInterface {
        public:
            virtual ~CommandCategoryInterface() = default;

        protected:
            explicit CommandCategoryInterface(DriverInterface *driver);

            /**
             * @brief Invia un comando generico attraverso il DriverInterface.
             * @param category Categoria del comando (es: 'M' per motion).
             * @param code Codice numerico del comando.
             * @param params Parametri del comando come stringhe.
             */
            core::types::Result sendCommand(char category, int code, const std::vector<std::string> &params) const;

            DriverInterface *driver_;
        };

    } // namespace printer-command
} // namespace core
