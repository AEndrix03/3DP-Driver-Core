#include "translator/dispatchers/compat/CompatibilityDispatcher.hpp"
#include "translator/GCodeSupport.hpp"

#include <chrono>
#include <stdexcept>
#include <thread>

namespace translator::gcode {

    CompatibilityDispatcher::CompatibilityDispatcher(std::shared_ptr<core::DriverInterface> driver)
            : driver_(std::move(driver)) {}

    bool CompatibilityDispatcher::canHandle(const std::string &command) const {
        if (command == "G4" || command == "M400" || command == "M204" || command == "M900") return true;
        const Support support = classify(command).support;
        return support == Support::Ignored || support == Support::Fatal;
    }

    bool CompatibilityDispatcher::validate(const std::string &, const std::map<std::string, double> &) const {
        return true;
    }

    void CompatibilityDispatcher::handle(const std::string &command, const std::map<std::string, double> &params) {
        if (command == "G4" || command == "M400") {
            // La pausa conta da quando la macchina e' ferma, non da quando il comando viene letto
            if (!driver_->waitForIdle(std::chrono::minutes(10))) {
                throw std::runtime_error(command + ": the machine did not finish its moves");
            }
            if (command == "G4") {
                double milliseconds = 0.0;
                if (params.count("P")) milliseconds = params.at("P");
                else if (params.count("S")) milliseconds = params.at("S") * 1000.0;
                if (milliseconds > 0.0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(milliseconds)));
                }
            }
            return;
        }

        // Parametri del piano di movimento: valgono dai movimenti successivi
        if (command == "M900") {
            if (params.count("K")) driver_->motion()->setLinearAdvance(params.at("K"));
            return;
        }
        if (command == "M204") {
            double print = 0.0, travel = 0.0;
            if (params.count("S")) print = travel = params.at("S");
            if (params.count("P")) print = params.at("P");
            if (params.count("T")) travel = params.at("T");
            driver_->motion()->setAccelerations(print, travel);
            return;
        }

        const SupportInfo info = classify(command);
        if (info.support == Support::Fatal) {
            // Eccezione generica: la coda la tratta come guasto e ferma il job
            throw std::runtime_error(command + ": " + info.note);
        }
        // Support::Ignored: riconosciuto, nessun effetto
    }

} // namespace translator::gcode
