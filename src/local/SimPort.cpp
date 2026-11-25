#include "SimPort.hpp"

#ifdef THREEDP_SIM

#include "SimBoard.hpp" // dal repo del firmware: test/native

#include <algorithm>
#include <atomic>

namespace local {
    namespace {
        std::atomic<double> speed_{1.0};
        std::atomic<long> endstop_[3] = {{-800}, {-800}, {-400}};

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
        for (int a = 0; a < 3; ++a) sim::setEndstop(a, endstop_[a]);
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

    void simShiftEndstop(int axis, long steps) {
        if (axis < 0 || axis > 2) return;
        endstop_[axis] += steps;
        sim::setEndstop(axis, endstop_[axis]);
    }
}

#else

namespace local {
    bool simAvailable() { return false; }

    std::shared_ptr<core::SerialPort> openSim(double) { return nullptr; }

    void simReady() {}

    void simSetSpeed(double) {}

    double simSpeed() { return 1.0; }

    void simShiftEndstop(int, long) {}
}

#endif
