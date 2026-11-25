#pragma once

#include "core/serial/SerialPort.hpp"
#include <memory>

namespace local {

    /**
     * @brief Stampante simulata: il firmware vero, compilato per il PC, al posto della scheda.
     *
     * Serve a provare pagina, file e tempi senza hardware: stessi comandi, stesso protocollo,
     * stesso generatore di step; finecorsa e ugello sono modelli. Disponibile solo se "3dp" e'
     * stato compilato con `tools/build_local.sh --sim` (servono i sorgenti del firmware).
     */
    bool simAvailable();

    /// Avvia il firmware simulato e restituisce la sua "porta seriale"; nullptr se non disponibile.
    std::shared_ptr<core::SerialPort> openSim(double speed);

    /// Da chiamare quando il firmware simulato ha finito l'avvio: attiva il termistore.
    void simReady();

    /// Quante volte piu' veloce del reale scorre il tempo della macchina simulata.
    void simSetSpeed(double speed);

    double simSpeed();

    /// Sposta di `steps` il finecorsa simulato di un asse (0=X, 1=Y, 2=Z): al prossimo homing la
    /// macchina lo trova fuori posto, come se avesse perso quegli step. Serve a vedere una misura fallire.
    void simShiftEndstop(int axis, long steps);
}
