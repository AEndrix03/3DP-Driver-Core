//
// Created by redeg on 26/04/2025.
//

#include "core/SerialPort.hpp"
#include <iostream>
#include <queue>
#include <string>

namespace core {

/**
 * @brief MockSerialPort: implementazione finta per test iniziali.
 */
    class MockSerialPort : public SerialPort {
    public:
        /**
         * @brief Invia dati sulla porta seriale mockata.
         */
        void send(const std::string &data) override {
            std::cout << "[Serial TX]: " << data << std::endl;
            // Iniettiamo una risposta finta "OK N<num>" per ogni comando
            if (data.find("N") == 0) {
                size_t posSpace = data.find(' ');
                if (posSpace != std::string::npos) {
                    std::string numberStr = data.substr(1, posSpace - 1);
                    responses_.push("OK N" + numberStr);
                }
            }
        }

        /**
         * @brief Riceve dati dalla porta seriale mockata.
         */
        std::string receiveLine() override {
            if (responses_.empty()) {
                return ""; // Simula timeout se vuoto
            }
            std::string response = responses_.front();
            responses_.pop();
            std::cout << "[Serial RX]: " << response << std::endl;
            return response;
        }

    private:
        std::queue<std::string> responses_;
    };

} // namespace core
