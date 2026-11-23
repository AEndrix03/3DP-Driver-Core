#pragma once

#include "translator/GCodeSupport.hpp"
#include <map>
#include <string>

namespace local {

    /// Volume di lavoro usato dai controlli (deve coincidere con MotionConfig.hpp del firmware).
    constexpr double VOLUME_X = 220.0, VOLUME_Y = 220.0, VOLUME_Z = 200.0;

    /// Il controllo preventivo di un file, in JSON.
    std::string reportJson(const translator::gcode::FileReport &report);

    struct SliceResult {
        bool ok = false;
        std::string message;
        std::string gcodePath;
        std::string parameters; ///< parametri usati, per lo storico
        std::string log;        ///< ultime righe dell'uscita dello slicer
    };

    /**
     * @brief Esegue PrusaSlicer da riga di comando con il profilo della macchina.
     *
     * Eseguibile: variabile THREEDP_SLICER, altrimenti "prusa-slicer" (su Windows
     * "prusa-slicer-console.exe"). Profilo: THREEDP_SLICER_PROFILE, altrimenti
     * config/slicer/3dp-prusaslicer.ini.
     *
     * @param options parametri ammessi: layer, infill, temp, perimeters, speed, supports, brim.
     *        Qualunque altra chiave viene rifiutata (nessun testo arbitrario arriva alla shell).
     */
    SliceResult slice(const std::string &modelPath, const std::string &outputPath,
                      const std::map<std::string, std::string> &options);
}
