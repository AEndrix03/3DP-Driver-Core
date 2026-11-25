#pragma once

#include "Settings.hpp"
#include "WirePort.hpp"
#include "core/DriverInterface.hpp"
#include "local/History.hpp"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace local {

    /**
     * @brief Misure che la macchina fa su se stessa, e prova di resistenza della meccanica.
     *
     * L'unico sensore di posizione e' il finecorsa: dopo ogni prova si rifa' l'homing e il firmware
     * dice dove credeva di essere quando il finecorsa e' scattato. Su una macchina che non ha perso
     * step e' zero; altrimenti e' il numero di step persi, con la precisione dell'interruttore
     * (che si misura anch'essa, con la prova di ripetibilita'). L'altro sensore e' il termistore.
     *
     * Cio' che richiede uno strumento esterno (un calibro per gli step per millimetro) non viene
     * finto: per quello c'e' solo il conto, a partire dal valore misurato dall'operatore.
     *
     * Una misura alla volta, in un thread suo: la pagina resta libera e puo' annullarla.
     */
    class Measure {
    public:
        using Params = std::map<std::string, std::string>;

        struct Host {
            std::shared_ptr<core::DriverInterface> driver;
            std::shared_ptr<WirePort> wire;
            Settings *settings = nullptr;
            History *history = nullptr;
            std::function<void()> restore; ///< rimette nel driver i valori della configurazione
            double timeScale = 1.0;        ///< >1 con la stampante simulata accelerata
        };

        explicit Measure(Host host);

        ~Measure();

        /// Avvia una misura. @return "" se e' partita, altrimenti il motivo del rifiuto
        std::string start(const Params &params);

        /// Chiede di fermarsi: la misura si interrompe al prossimo movimento.
        void cancel();

        bool active() const { return active_.load(); }

        void setTimeScale(double scale) { timeScale_ = scale; }

        /// Stato o ultimo risultato, per la pagina e la riga di comando.
        std::string json();

        /// Step per millimetro corretti dal valore misurato dall'operatore: solo il conto, nessun movimento.
        std::string stepsAdvice(const Params &params, bool &ok);

    private:
        struct Row {
            std::string section, label, value, verdict; ///< verdict: ok, warn, bad o vuoto
        };

        Host host_;
        std::thread worker_;
        std::atomic<bool> active_{false}, cancel_{false};
        std::atomic<double> timeScale_{1.0};

        std::mutex mutex_; // protegge tutto cio' che segue
        std::string kind_, title_, phase_, verdict_, summary_, jobId_;
        double progress_ = 0.0;
        long long startedAt_ = 0, finishedAt_ = 0;
        std::vector<Row> rows_;
        std::vector<std::string> log_;
        std::map<std::string, std::string> suggest_;

        std::string section_; // usati solo dal thread della misura
        bool heating_ = false;

        void run(Params params);

        // --- passi comuni ---
        void guard();

        void phase(const std::string &text, double progress = -1.0);

        void log(const std::string &text);

        void row(const std::string &label, const std::string &value, const std::string &verdict = "");

        void suggest(const std::string &key, double value);

        void wait(double machineSeconds);

        void settle();

        void go(int axis, double position, double speed, bool pipelined = false);

        void goXY(double x, double y, double speed);

        void reference(const std::string &axes);

        long homeOffset(int axis);

        double travel(int axis);

        double temperature(double *target = nullptr);

        // --- misure ---
        bool repeatability(int axis, int count);

        bool speedLadder(int axis, double distance, double from, double to, int levels, int repeats, long tolerance);

        bool accelLadder(int axis, double distance, double from, double to, int levels, int repeats, long tolerance);

        bool travelCheck(int axis, double speed, long tolerance);

        bool sensorNoise(double seconds);

        bool heatTest(double target, double hold, double timeout);

        bool stress(double minutes, double checkMinutes, bool withZ, unsigned seed, long tolerance, bool blind);

        void healthRows(const std::function<void()> &body);
    };
}
