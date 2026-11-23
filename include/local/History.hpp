#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

namespace local {

/// Una stampa nello storico locale.
    struct PrintRecord {
        int64_t id = 0;
        std::string jobId;
        std::string name;
        std::string gcodePath;
        std::string sourcePath;  ///< modello di partenza (STL), se la stampa nasce da uno slicing
        std::string slicerParams;
        std::string state;       ///< RUNNING, COMPLETED, FAILED, CANCELLED, ...
        std::string error;
        int64_t startedAt = 0;   ///< secondi dall'epoca Unix
        int64_t finishedAt = 0;
        int64_t lines = 0;
    };

/**
 * @brief Storico delle stampe su SQLite: un solo file locale, nessun servizio esterno.
 */
    class History {
    public:
        explicit History(const std::string &path);

        ~History();

        bool isOpen() const { return db_ != nullptr; }

        const std::string &lastError() const { return error_; }

        int64_t add(const PrintRecord &record);

        void finish(const std::string &jobId, const std::string &state, const std::string &error);

        /// Stampe rimaste "in corso" da un'esecuzione precedente del servizio.
        void closeStale(const std::string &state);

        std::vector<PrintRecord> list(int limit);

    private:
        sqlite3 *db_ = nullptr;
        std::mutex mutex_;
        std::string error_;

        bool exec(const char *sql);
    };
} // namespace local
