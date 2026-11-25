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
        std::string warning;    ///< avviso dello slicer su uno slicing riuscito ("" se nessuno)
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
    /// Dove sono lo slicer e il profilo della macchina, e se ci sono davvero.
    struct SlicerSetup {
        std::string binary;  ///< percorso dell'eseguibile, o il suo nome se va cercato nel PATH al momento
        std::string profile;
        bool binaryFound = false;
        bool profileFound = false;
    };

    /**
     * @brief Trova slicer e profilo senza che l'operatore debba impostare nulla.
     *
     * Eseguibile, nell'ordine: THREEDP_SLICER; una copia portable in `tools/slicer/` o in
     * `../_tools/` (cartelle "PrusaSlicer*"); l'installazione standard di Windows; il PATH.
     * Profilo: THREEDP_SLICER_PROFILE, altrimenti `config/slicer/3dp-prusaslicer.ini`.
     * I percorsi relativi valgono sia dalla cartella di lavoro sia da quella sopra l'eseguibile,
     * cosi' il servizio funziona da qualunque cartella venga avviato.
     */
    SlicerSetup locateSlicer();

    SliceResult slice(const std::string &modelPath, const std::string &outputPath,
                      const std::map<std::string, std::string> &options);
}
