//
// Created by redeg on 26/04/2025.
//

#pragma once

#include <string>
#include <vector>

namespace core {

/**
 * @brief Costruisce stringhe di comando complete da inviare alla stampante.
 */
    class CommandBuilder {
    public:
        /**
         * @brief Costruisce un comando completo dato numero, categoria, codice e parametri.
         * @param number Numero del comando.
         * @param category Categoria comando (es. 'M', 'A', 'T').
         * @param code Codice comando numerico.
         * @param params Parametri addizionali come lista di stringhe.
         * @return Comando formattato con checksum.
         */
        static std::string
        buildCommand(uint16_t number, char category, int code, const std::vector<std::string> &params);

    private:
        static uint8_t computeChecksum(const std::string &data);
    };

}
