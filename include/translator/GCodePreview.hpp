#pragma once

#include "core/motion/MotionPlanner.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace translator::gcode {

    /// Fine di un tratto rettilineo del percorso (gli archi sono gia' spezzati in corde).
    struct PreviewPoint {
        float x = 0, y = 0, z = 0;
        double seconds = 0.0;   ///< tempo previsto dall'inizio all'arrivo in questo punto
        uint32_t command = 0;   ///< quanti comandi del file sono stati letti per arrivarci (come li conta il job)
        bool extruding = false; ///< il tratto che arriva qui deposita materiale
    };

    /**
     * @brief Percorso e tempi di un file G-code, senza stampante.
     *
     * Il file passa per lo stesso pianificatore usato in stampa, con gli stessi limiti: le
     * durate sono quelle del piano di velocita' che verrebbe inviato al firmware, non una
     * stima a velocita' costante. Restano fuori i tempi che non dipendono dal movimento:
     * riscaldamento (M109) e homing (G28), che vengono solo contati.
     */
    struct Preview {
        bool readable = false;
        std::vector<PreviewPoint> points;
        double motionSeconds = 0.0; ///< solo movimenti
        double dwellSeconds = 0.0;  ///< pause chieste dal file (G4)
        int heatWaits = 0;          ///< attese di temperatura (durata non stimata)
        int homings = 0;            ///< homing (durata non stimata)

        double seconds() const { return motionSeconds + dwellSeconds; }
    };

    /// @param speedFactor, blockRate le stesse regolazioni del pianificatore in stampa (1 e 0 = nessuna)
    Preview buildPreview(const std::string &path, const core::planning::MachineLimits &limits, double advanceK,
                         double speedFactor = 1.0, double blockRate = 0.0);

    /// JSON compatto per la pagina: "pts" e' una lista piatta di interi, sei per punto:
    /// x, y, z in centesimi di mm, tempo in centesimi di secondo, numero del comando, 1 se estrude.
    std::string previewJson(const Preview &preview);
}
