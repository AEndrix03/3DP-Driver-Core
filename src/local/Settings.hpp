#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace local {

    /// Una voce di configurazione, con cio' che serve alla pagina per mostrarla e controllarla.
    struct Setting {
        const char *key;
        const char *group;
        const char *label;
        const char *unit;
        double min, max, step, fallback;
        bool text;        ///< valore testuale (percorsi): min/max/step non valgono
        bool whilePrinting; ///< si puo' cambiare a stampa in corso
        const char *help;
    };

    /**
     * @brief Configurazione del driver locale: un file `chiave=valore` nella cartella dei dati.
     *
     * E' l'unico posto in cui stanno i valori scelti dall'operatore; il servizio li legge
     * all'avvio e a ogni modifica. I valori fissati nel firmware (step/mm, coda, ...) non sono
     * qui: il driver li legge dalla scheda e la pagina li mostra in sola lettura.
     */
    class Settings {
    public:
        explicit Settings(const std::string &path);

        static const std::vector<Setting> &catalog();

        double number(const std::string &key);

        std::string text(const std::string &key);

        /// @return "" se accettato, altrimenti il motivo del rifiuto (nulla viene cambiato)
        std::string set(const std::string &key, const std::string &value);

        bool save();

        /// Le voci con i valori attuali, in JSON ("groups": [...]).
        std::string json();

    private:
        std::string path_;
        std::mutex mutex_;
        std::map<std::string, std::string> values_;

        static const Setting *find(const std::string &key);
    };
}
