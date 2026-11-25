#include "SimPort.hpp"

#ifdef THREEDP_SIM

#include "SimBoard.hpp" // dal repo del firmware: test/native

#include <algorithm>
#include <atomic>

namespace local {
    namespace {
        std::atomic<double> speed_{1.0};

        class SimPort : public core::SerialPort {
        public:
            void send(const std::string &data) override { sim::hostWrite(data + "\n"); }

            void sendRaw(const std::string &bytes) override { sim::hostWrite(bytes); }

            std::string receiveLine() override { return sim::hostReadLine(40); }

            bool isOpen() const override { return true; }
        };
    }

    bool simAvailable() { return true; }

    std::shared_ptr<core::SerialPort> openSim(double speed) {
        sim::start();
        // Finecorsa a 10 mm (X, Y) e 1 mm (Z) sotto il punto di accensione: l'homing li trova
        sim::setEndstop(0, -800);
        sim::setEndstop(1, -800);
        sim::setEndstop(2, -400);
        // Ugello: modello termico pilotato dal pin dell'heater
        sim::enableThermalModel(true, 25.0);
        simSetSpeed(speed);
        return std::make_shared<SimPort>();
    }

    void simReady() {
        // Dopo l'avvio: l'inizializzazione del firmware riporta il termistore al valore di Config.hpp
        sim::enableThermistor(true);
    }

    void simSetSpeed(double speed) {
        speed_ = std::clamp(speed, 0.25, 60.0);
        sim::setTimeScale(speed_);
    }

    double simSpeed() { return speed_; }
}

#else

namespace local {
    bool simAvailable() { return false; }

    std::shared_ptr<core::SerialPort> openSim(double) { return nullptr; }

    void simReady() {}

    void simSetSpeed(double) {}

    double simSpeed() { return 1.0; }
}

#endif
