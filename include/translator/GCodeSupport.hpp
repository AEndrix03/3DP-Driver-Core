#pragma once

#include <map>
#include <string>
#include <vector>

namespace translator::gcode {

/**
 * @brief Cosa fa il driver con ciascun comando G-code: un solo elenco, usato sia
 * dall'esecuzione (CompatibilityDispatcher) sia dal controllo preventivo dei file.
 */
    enum class Support {
        Executed, ///< eseguito
        Ignored,  ///< riconosciuto e saltato di proposito (non serve o non si applica a questa macchina)
        Unknown,  ///< non riconosciuto: saltato con un avviso
        Fatal     ///< la stampa uscirebbe sbagliata: il job si ferma
    };

    struct SupportInfo {
        Support support;
        std::string note;
    };

    /// @param command comando gia' normalizzato ("G1", "M104", "T0", "M862.3")
    SupportInfo classify(const std::string &command);

    /// Primo token della riga, maiuscolo, senza zeri iniziali ("g01" -> "G1"); "" per righe vuote o commenti.
    std::string commandOf(const std::string &line);

/// Esito del controllo preventivo di un file G-code.
    struct FileReport {
        bool readable = false;
        size_t lines = 0;
        size_t commands = 0;
        size_t moves = 0;
        size_t arcs = 0;
        size_t layers = 0;
        bool homes = false;             ///< contiene G28 prima del primo movimento
        double maxHotend = 0.0;         ///< temperatura massima richiesta
        double minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
        bool hasBounds = false;
        double filamentMm = 0.0;        ///< filamento estruso (mm)
        std::map<std::string, size_t> ignored; ///< comando -> occorrenze
        std::map<std::string, size_t> unknown;
        std::map<std::string, size_t> fatal;
        std::vector<std::string> problems;     ///< motivi per cui il file NON va stampato
        std::vector<std::string> warnings;

        bool printable() const { return readable && problems.empty(); }
    };

    /**
     * @brief Legge un file G-code senza eseguirlo e dice cosa ne farebbe il driver.
     * @param volumeX,volumeY,volumeZ volume di lavoro (mm); 0 = non controllare
     */
    FileReport checkFile(const std::string &path, double volumeX, double volumeY, double volumeZ);
}
