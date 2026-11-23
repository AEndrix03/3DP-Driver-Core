#pragma once

#include "translator/dispatchers/ICommandDispatcher.hpp"
#include "core/DriverInterface.hpp"
#include <memory>

namespace translator::gcode {

/**
 * @brief Comandi "di contorno" che gli slicer mettono in ogni file.
 *
 *  - G4 (pausa) e M400 (attendi la fine dei movimenti) vengono eseguiti;
 *  - quelli che non servono o non si applicano a questa macchina (G21, M73, M201...,
 *    T0, M84, ...) vengono riconosciuti e saltati, senza avvisi a ogni riga;
 *  - quelli con cui la stampa uscirebbe sbagliata (G20 pollici, T1, G29, M600, ...)
 *    fermano il job con il motivo.
 *
 * L'elenco e' in GCodeSupport.cpp, lo stesso usato dal controllo preventivo dei file.
 * Va registrato per ultimo: i comandi che hanno un dispatcher proprio passano prima da quello.
 */
    class CompatibilityDispatcher : public ICommandDispatcher {
    public:
        explicit CompatibilityDispatcher(std::shared_ptr<core::DriverInterface> driver);

        bool canHandle(const std::string &command) const override;

        bool validate(const std::string &command, const std::map<std::string, double> &params) const override;

        void handle(const std::string &command, const std::map<std::string, double> &params) override;

    private:
        std::shared_ptr<core::DriverInterface> driver_;
    };

} // namespace translator::gcode
