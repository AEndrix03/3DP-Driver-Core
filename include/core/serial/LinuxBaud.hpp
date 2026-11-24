#pragma once

namespace core {
    /// Baud della scheda: 250000 e' l'unico, tra quelli veloci, che un quarzo da 16 MHz genera
    /// esatto (115200 diventa 117647, +2,1%: vedi PROTOCOL.md del firmware, "Velocita' della linea").
    constexpr unsigned DEFAULT_SERIAL_BAUD = 250000;

    /**
     * @brief Imposta un baud qualsiasi su Linux (250000 non e' tra le costanti di termios).
     * @return false se non riesce, o se non si e' su Linux
     */
    bool setLinuxBaud(int fd, unsigned baud);
}
