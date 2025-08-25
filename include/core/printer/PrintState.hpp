#pragma once

namespace core {
    enum class PrintState {
        Idle, // Stampante in attesa
        Homing, // Processo di home degli assi
        Printing, // Stampante sta stampando
        Paused, // Stampante in pausa
        Error // Stampante in errore
    };
}
