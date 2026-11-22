#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace core::print {

    /**
     * @brief Punto di ripresa di un job di stampa.
     *
     * Descrive la prima riga di G-code NON ancora completata dalla macchina e lo
     * stato in cui la macchina si trovava all'inizio di quella riga. Viene salvato
     * su disco durante la stampa: sopravvive a guasti di comunicazione, riavvii del
     * firmware e riavvii del driver.
     *
     * "Completata" significa eseguita fisicamente: il firmware accoda i movimenti e
     * risponde OK prima di eseguirli, quindi il checkpoint segue il campo D delle
     * risposte (ultimo comando terminato), non l'ultimo comando inviato.
     */
    struct JobCheckpoint {
        std::string jobId;
        std::string gcodePath;
        size_t nextLine = 0;       ///< indice (da 0) della riga del file da cui ripartire

        bool positionKnown = false;
        double x = 0.0, y = 0.0, z = 0.0; ///< posizione all'inizio di nextLine (mm)
        double e = 0.0;            ///< valore logico di E (come lo vede il G-code)
        double feedrate = 1500.0;  ///< mm/min
        bool absoluteXYZ = true;
        bool absoluteE = true;

        double hotendTarget = 0.0;
        int fanSpeed = 0;
        int layer = 0;

        /** Scrive il checkpoint in modo atomico (file temporaneo + rename). */
        bool save() const;

        /** Elimina il checkpoint del job. */
        static void remove(const std::string &jobId);

        static std::optional<JobCheckpoint> load(const std::string &jobId);

        /** Il checkpoint piu' recente tra quelli presenti su disco. */
        static std::optional<JobCheckpoint> latest();

        /** Cartella dei checkpoint (creata al primo salvataggio). */
        static std::string directory();
    };

} // namespace core::print
