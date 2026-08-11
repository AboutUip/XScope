#include "xscope/storage/database.hpp"

#include "sqlite3.h"

#include <utility>

namespace xscope::storage {

Database::~Database() { close(); }

Database::Database(Database&& other) noexcept {
    std::scoped_lock lock(other.mu_);
    db_ = other.db_;
    other.db_ = nullptr;
}

Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        close();
        std::scoped_lock lock(other.mu_);
        db_ = other.db_;
        other.db_ = nullptr;
    }
    return *this;
}

void Database::open(const std::filesystem::path& path) {
    std::scoped_lock lock(mu_);
    if (db_) {
        throw DatabaseError("database already open");
    }
    sqlite3* handle = nullptr;
    // Use UTF-16 open on Windows so non-ASCII data_root paths work.
    const int rc = sqlite3_open16(path.wstring().c_str(), &handle);
    if (rc != SQLITE_OK) {
        const char* msg = handle ? sqlite3_errmsg(handle) : "sqlite3_open16 failed";
        std::string full = msg;
        full.append(" (");
        const auto u8 = path.u8string();
        full.append(reinterpret_cast<const char*>(u8.c_str()), u8.size());
        full.append(")");
        if (handle) {
            sqlite3_close(handle);
        }
        throw DatabaseError(full);
    }
    db_ = handle;
    char* err = nullptr;
    if (sqlite3_exec(db_, "PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL; PRAGMA busy_timeout=5000;",
                     nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "pragma failed";
        sqlite3_free(err);
        sqlite3_close(db_);
        db_ = nullptr;
        throw DatabaseError(msg);
    }
}

void Database::close() noexcept {
    std::scoped_lock lock(mu_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::is_open() const noexcept {
    std::scoped_lock lock(mu_);
    return db_ != nullptr;
}

void Database::throw_sqlite(std::string_view what) const {
    std::string msg(what);
    if (db_) {
        msg.append(": ");
        msg.append(sqlite3_errmsg(db_));
    }
    throw DatabaseError(msg);
}

void Database::exec(std::string_view sql) {
    std::scoped_lock lock(mu_);
    if (!db_) {
        throw DatabaseError("database is closed");
    }
    char* err = nullptr;
    const int rc = sqlite3_exec(db_, std::string(sql).c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "exec failed";
        sqlite3_free(err);
        throw DatabaseError(msg);
    }
}

int Database::user_version() {
    auto rows = query("PRAGMA user_version");
    if (rows.empty() || rows[0].empty() || !rows[0][0]) {
        return 0;
    }
    return std::stoi(*rows[0][0]);
}

void Database::set_user_version(int version) {
    exec("PRAGMA user_version=" + std::to_string(version));
}

void Database::migrate(int target_version, const std::function<void(Database&, int, int)>& migrator) {
    const int from = user_version();
    if (from > target_version) {
        throw DatabaseError("database schema is newer than this SDK");
    }
    if (from == target_version) {
        return;
    }
    with_transaction([&] {
        migrator(*this, from, target_version);
        set_user_version(target_version);
    });
}

void Database::execute(std::string_view sql, const Binder& bind) {
    std::scoped_lock lock(mu_);
    if (!db_) {
        throw DatabaseError("database is closed");
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, std::string(sql).c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw_sqlite("prepare");
    }
    if (bind) {
        bind(stmt);
    }
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        throw_sqlite("step");
    }
}

std::vector<Database::Row> Database::query(std::string_view sql, const Binder& bind) {
    std::scoped_lock lock(mu_);
    if (!db_) {
        throw DatabaseError("database is closed");
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, std::string(sql).c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw_sqlite("prepare");
    }
    if (bind) {
        bind(stmt);
    }
    std::vector<Row> rows;
    const int cols = sqlite3_column_count(stmt);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Row row;
        row.reserve(static_cast<size_t>(cols));
        for (int i = 0; i < cols; ++i) {
            if (sqlite3_column_type(stmt, i) == SQLITE_NULL) {
                row.emplace_back(std::nullopt);
            } else if (sqlite3_column_type(stmt, i) == SQLITE_BLOB) {
                const auto* p = static_cast<const char*>(sqlite3_column_blob(stmt, i));
                const int n = sqlite3_column_bytes(stmt, i);
                row.emplace_back(std::string(p, p + n));
            } else {
                const unsigned char* text = sqlite3_column_text(stmt, i);
                row.emplace_back(text ? reinterpret_cast<const char*>(text) : "");
            }
        }
        rows.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
    return rows;
}

void Database::with_transaction(const std::function<void()>& fn) {
    exec("BEGIN IMMEDIATE");
    try {
        fn();
        exec("COMMIT");
    } catch (...) {
        try {
            exec("ROLLBACK");
        } catch (...) {
        }
        throw;
    }
}

void Database::bind_text(sqlite3_stmt* stmt, int index, std::string_view value) {
    sqlite3_bind_text(stmt, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

void Database::bind_int64(sqlite3_stmt* stmt, int index, std::int64_t value) {
    sqlite3_bind_int64(stmt, index, value);
}

void Database::bind_double(sqlite3_stmt* stmt, int index, double value) {
    sqlite3_bind_double(stmt, index, value);
}

void Database::bind_blob(sqlite3_stmt* stmt, int index, const void* data, int size) {
    sqlite3_bind_blob(stmt, index, data, size, SQLITE_TRANSIENT);
}

void Database::bind_null(sqlite3_stmt* stmt, int index) { sqlite3_bind_null(stmt, index); }

} // namespace xscope::storage
