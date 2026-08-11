#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace xscope::storage {

class DatabaseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Thread-safe SQLite wrapper: parameterized SQL only, short transactions.
class Database {
public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;

    void open(const std::filesystem::path& path);
    void close() noexcept;
    bool is_open() const noexcept;

    void exec(std::string_view sql);
    void migrate(int target_version, const std::function<void(Database&, int from, int to)>& migrator);

    int user_version();
    void set_user_version(int version);

    using Row = std::vector<std::optional<std::string>>;
    using Binder = std::function<void(sqlite3_stmt*)>;

    void execute(std::string_view sql, const Binder& bind = {});
    std::vector<Row> query(std::string_view sql, const Binder& bind = {});

    void with_transaction(const std::function<void()>& fn);

    sqlite3* raw() noexcept { return db_; }

    static void bind_text(sqlite3_stmt* stmt, int index, std::string_view value);
    static void bind_int64(sqlite3_stmt* stmt, int index, std::int64_t value);
    static void bind_blob(sqlite3_stmt* stmt, int index, const void* data, int size);
    static void bind_null(sqlite3_stmt* stmt, int index);

private:
    void throw_sqlite(std::string_view what) const;

    sqlite3* db_ = nullptr;
    mutable std::recursive_mutex mu_;
};

} // namespace xscope::storage
