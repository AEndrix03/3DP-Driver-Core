#pragma once

#include <optional>
#include <string>
#include <vector>

/**
 * Conti delle misure automatiche, senza macchina: si possono provare da soli.
 */
namespace local::measure {

    struct Stats {
        int count = 0;
        double mean = 0.0, deviation = 0.0, min = 0.0, max = 0.0;
    };

    Stats stats(const std::vector<double> &values);

    /// Esito dell'homing asse per asse (0=X, 1=Y, 2=Z), letto dalle righe "HOM <asse> <esito> [step]".
    struct Homing {
        bool done[3] = {false, false, false};    ///< finecorsa trovato
        bool skipped[3] = {false, false, false}; ///< finecorsa disabilitato nel firmware
        bool failed[3] = {false, false, false};  ///< finecorsa mai scattato
        /// Posizione (step) che il firmware credeva di avere al contatto: gli step persi dall'homing
        /// precedente. Assente se il firmware non l'ha misurata.
        std::optional<long> offset[3];
    };

    Homing parseHoming(const std::vector<std::string> &body);

    /// Cio' che la scheda dice di se' con S11 (righe DIA e PRF).
    struct Health {
        bool valid = false;
        long checksumErrors = 0, overflows = 0, lostAcks = 0;
        long freeRamMin = 0, stepInterruptMaxUs = 0, missedDeadlines = 0, uartErrors = 0;
    };

    Health parseHealth(const std::vector<std::string> &body);

    /// Velocita' davvero raggiunta (mm/s) su un tratto da fermo a fermo.
    double peakSpeed(double requested, double acceleration, double distance);

    struct TempSample {
        double t = 0.0;       ///< secondi
        double celsius = 0.0;
    };

    /// Guadagni proposti per il PID dell'hotend, dalla salita a piena potenza.
    struct PidProposal {
        bool valid = false;
        std::string why;       ///< perche' non e' valida
        double rate = 0.0;     ///< pendenza massima (gradi/s) a piena potenza
        double deadTime = 0.0; ///< ritardo (s) tra l'accensione e l'inizio della salita
        double kp = 0.0, ki = 0.0, kd = 0.0;
    };

    /**
     * Dalla curva di riscaldamento ricava ritardo e pendenza massima e ne deduce i guadagni.
     * Usa solo i campioni sotto `fullPowerBelow` gradi: li' l'heater e' sempre acceso, quindi la
     * curva dice com'e' fatto l'hotend e non com'e' regolato.
     */
    PidProposal tuneFromRise(const std::vector<TempSample> &samples, double fullPowerBelow);
}
