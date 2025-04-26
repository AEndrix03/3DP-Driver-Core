//
// Created by redeg on 26/04/2025.
//

#pragma once

#include <string>

namespace core {

/**
 * @brief Interfaccia per la comunicazione seriale con la stampante.
 */
    class SerialPort {
    public:
        virtual ~SerialPort() = default;

        /**
         * @brief Invia una stringa sulla porta seriale.
         * @param data Stringa da inviare.
         */
        virtual void send(const std::string &data) = 0;

        /**
         * @brief Riceve una linea dalla porta seriale.
         * @return Stringa ricevuta.
         */
        virtual std::string receiveLine() = 0;
    };

} // namespace core
