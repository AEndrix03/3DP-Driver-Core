#include "local/History.hpp"

#include <ctime>
#include <sqlite3.h>

namespace local {
    namespace {
        std::string column(sqlite3_stmt *statement, int index) {
            const unsigned char *text = sqlite3_column_text(statement, index);
            return text ? reinterpret_cast<const char *>(text) : "";
        }

        void bind(sqlite3_stmt *statement, int index, const std::string &value) {
            sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT);
        }
    }

    History::History(const std::string &path) {
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
            error_ = db_ ? sqlite3_errmsg(db_) : "cannot open database";
            if (db_) sqlite3_close(db_);
            db_ = nullptr;
            return;
        }
        exec("PRAGMA journal_mode=WAL");
        exec("CREATE TABLE IF NOT EXISTS prints ("
             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
             "job_id TEXT NOT NULL UNIQUE,"
             "name TEXT NOT NULL,"
             "gcode_path TEXT NOT NULL,"
             "source_path TEXT NOT NULL DEFAULT '',"
             "slicer_params TEXT NOT NULL DEFAULT '',"
             "state TEXT NOT NULL,"
             "error TEXT NOT NULL DEFAULT '',"
             "started_at INTEGER NOT NULL,"
             "finished_at INTEGER NOT NULL DEFAULT 0,"
             "lines INTEGER NOT NULL DEFAULT 0)");
    }

    History::~History() {
        if (db_) sqlite3_close(db_);
    }

    bool History::exec(const char *sql) {
        char *message = nullptr;
        if (sqlite3_exec(db_, sql, nullptr, nullptr, &message) != SQLITE_OK) {
            error_ = message ? message : "sqlite error";
            sqlite3_free(message);
            return false;
        }
        return true;
    }

    int64_t History::add(const PrintRecord &record) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return 0;
        sqlite3_stmt *statement = nullptr;
        const char *sql = "INSERT INTO prints (job_id, name, gcode_path, source_path, slicer_params, state, "
                          "started_at, lines) VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
        if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) return 0;
        bind(statement, 1, record.jobId);
        bind(statement, 2, record.name);
        bind(statement, 3, record.gcodePath);
        bind(statement, 4, record.sourcePath);
        bind(statement, 5, record.slicerParams);
        bind(statement, 6, record.state);
        sqlite3_bind_int64(statement, 7, record.startedAt ? record.startedAt : static_cast<int64_t>(std::time(nullptr)));
        sqlite3_bind_int64(statement, 8, record.lines);
        const bool ok = sqlite3_step(statement) == SQLITE_DONE;
        sqlite3_finalize(statement);
        return ok ? sqlite3_last_insert_rowid(db_) : 0;
    }

    void History::finish(const std::string &jobId, const std::string &state, const std::string &error) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return;
        sqlite3_stmt *statement = nullptr;
        const char *sql = "UPDATE prints SET state = ?, error = ?, finished_at = ? WHERE job_id = ?";
        if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) return;
        bind(statement, 1, state);
        bind(statement, 2, error);
        sqlite3_bind_int64(statement, 3, static_cast<int64_t>(std::time(nullptr)));
        bind(statement, 4, jobId);
        sqlite3_step(statement);
        sqlite3_finalize(statement);
    }

    void History::closeStale(const std::string &state) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return;
        sqlite3_stmt *statement = nullptr;
        const char *sql = "UPDATE prints SET state = ?, finished_at = ? WHERE finished_at = 0";
        if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) return;
        bind(statement, 1, state);
        sqlite3_bind_int64(statement, 2, static_cast<int64_t>(std::time(nullptr)));
        sqlite3_step(statement);
        sqlite3_finalize(statement);
    }

    std::vector<PrintRecord> History::list(int limit) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<PrintRecord> records;
        if (!db_) return records;
        sqlite3_stmt *statement = nullptr;
        const char *sql = "SELECT id, job_id, name, gcode_path, source_path, slicer_params, state, error, "
                          "started_at, finished_at, lines FROM prints ORDER BY id DESC LIMIT ?";
        if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) return records;
        sqlite3_bind_int(statement, 1, limit);
        while (sqlite3_step(statement) == SQLITE_ROW) {
            PrintRecord r;
            r.id = sqlite3_column_int64(statement, 0);
            r.jobId = column(statement, 1);
            r.name = column(statement, 2);
            r.gcodePath = column(statement, 3);
            r.sourcePath = column(statement, 4);
            r.slicerParams = column(statement, 5);
            r.state = column(statement, 6);
            r.error = column(statement, 7);
            r.startedAt = sqlite3_column_int64(statement, 8);
            r.finishedAt = sqlite3_column_int64(statement, 9);
            r.lines = sqlite3_column_int64(statement, 10);
            records.push_back(std::move(r));
        }
        sqlite3_finalize(statement);
        return records;
    }
} // namespace local
