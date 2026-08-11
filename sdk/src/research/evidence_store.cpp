#include "xscope/research/evidence_store.hpp"

#include "sqlite3.h"
#include "xscope/utils/path.hpp"
#include "xscope/utils/time.hpp"

#include <stdexcept>

namespace xscope::research {
namespace {

ResearchRun run_from_row(const storage::Database::Row& row) {
    ResearchRun r;
    r.id = row.size() > 0 ? row[0].value_or("") : "";
    r.project_id = row.size() > 1 ? row[1].value_or("") : "";
    r.query = row.size() > 2 ? row[2].value_or("") : "";
    r.model_id = row.size() > 3 ? row[3].value_or("") : "";
    r.precision = precision_from_int(row.size() > 4 && row[4] ? std::stoi(*row[4]) : 1);
    r.status = run_status_from_string(row.size() > 5 ? row[5].value_or("pending") : "pending");
    r.search_rounds_done = row.size() > 6 && row[6] ? static_cast<int>(std::stoll(*row[6])) : 0;
    r.last_error = row.size() > 7 ? row[7].value_or("") : "";
    r.summary = row.size() > 8 ? row[8].value_or("") : "";
    r.waiting_prompt = row.size() > 9 ? row[9].value_or("") : "";
    r.created_at = row.size() > 10 && row[10] ? std::stoll(*row[10]) : 0;
    r.updated_at = row.size() > 11 && row[11] ? std::stoll(*row[11]) : 0;
    return r;
}

EvidenceItem evidence_from_row(const storage::Database::Row& row) {
    EvidenceItem e;
    e.id = row.size() > 0 ? row[0].value_or("") : "";
    e.run_id = row.size() > 1 ? row[1].value_or("") : "";
    e.kind = row.size() > 2 ? row[2].value_or("") : "";
    e.title = row.size() > 3 ? row[3].value_or("") : "";
    e.source_uri = row.size() > 4 ? row[4].value_or("") : "";
    e.module_id = row.size() > 5 ? row[5].value_or("") : "";
    e.snippet = row.size() > 6 ? row[6].value_or("") : "";
    e.body_json = row.size() > 7 ? row[7].value_or("") : "";
    e.round = row.size() > 8 && row[8] ? static_cast<int>(std::stoll(*row[8])) : 0;
    e.created_at = row.size() > 9 && row[9] ? std::stoll(*row[9]) : 0;
    return e;
}

} // namespace

void EvidenceStore::open(storage::Database& project_db, const std::filesystem::path& project_files_dir) {
    db_ = &project_db;
    files_dir_ = project_files_dir;
    utils::ensure_directory(files_dir_ / "evidence");
}

void EvidenceStore::close() noexcept {
    db_ = nullptr;
    files_dir_.clear();
}

void EvidenceStore::upsert_run(const ResearchRun& run) {
    if (!db_) {
        throw std::runtime_error("EvidenceStore is not open");
    }
    db_->execute(
        "INSERT INTO research_runs(id, project_id, query, model_id, precision, status, "
        "search_rounds_done, error, summary, waiting_prompt, created_at, updated_at) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "query=excluded.query, model_id=excluded.model_id, precision=excluded.precision, "
        "status=excluded.status, search_rounds_done=excluded.search_rounds_done, "
        "error=excluded.error, summary=excluded.summary, waiting_prompt=excluded.waiting_prompt, "
        "updated_at=excluded.updated_at",
        [&](sqlite3_stmt* stmt) {
            storage::Database::bind_text(stmt, 1, run.id);
            storage::Database::bind_text(stmt, 2, run.project_id);
            storage::Database::bind_text(stmt, 3, run.query);
            storage::Database::bind_text(stmt, 4, run.model_id);
            storage::Database::bind_int64(stmt, 5, precision_to_int(run.precision));
            storage::Database::bind_text(stmt, 6, run_status_to_string(run.status));
            storage::Database::bind_int64(stmt, 7, run.search_rounds_done);
            storage::Database::bind_text(stmt, 8, run.last_error);
            storage::Database::bind_text(stmt, 9, run.summary);
            storage::Database::bind_text(stmt, 10, run.waiting_prompt);
            storage::Database::bind_int64(stmt, 11, run.created_at);
            storage::Database::bind_int64(stmt, 12, run.updated_at);
        });
}

std::optional<ResearchRun> EvidenceStore::get_run(const std::string& run_id) {
    if (!db_) {
        return std::nullopt;
    }
    auto rows = db_->query(
        "SELECT id, project_id, query, model_id, precision, status, search_rounds_done, error, "
        "summary, waiting_prompt, created_at, updated_at FROM research_runs WHERE id=?",
        [&](sqlite3_stmt* stmt) { storage::Database::bind_text(stmt, 1, run_id); });
    if (rows.empty()) {
        return std::nullopt;
    }
    return run_from_row(rows[0]);
}

std::vector<ResearchRun> EvidenceStore::list_runs() {
    std::vector<ResearchRun> out;
    if (!db_) {
        return out;
    }
    auto rows = db_->query(
        "SELECT id, project_id, query, model_id, precision, status, search_rounds_done, error, "
        "summary, waiting_prompt, created_at, updated_at FROM research_runs "
        "ORDER BY created_at DESC");
    out.reserve(rows.size());
    for (const auto& row : rows) {
        out.push_back(run_from_row(row));
    }
    return out;
}

void EvidenceStore::upsert_evidence(const EvidenceItem& item) {
    if (!db_) {
        throw std::runtime_error("EvidenceStore is not open");
    }
    db_->execute(
        "INSERT INTO evidence(id, run_id, kind, title, source_uri, module_id, snippet, body_json, "
        "round, created_at) VALUES(?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "title=excluded.title, source_uri=excluded.source_uri, snippet=excluded.snippet, "
        "body_json=excluded.body_json, round=excluded.round",
        [&](sqlite3_stmt* stmt) {
            storage::Database::bind_text(stmt, 1, item.id);
            storage::Database::bind_text(stmt, 2, item.run_id);
            storage::Database::bind_text(stmt, 3, item.kind);
            storage::Database::bind_text(stmt, 4, item.title);
            storage::Database::bind_text(stmt, 5, item.source_uri);
            storage::Database::bind_text(stmt, 6, item.module_id);
            storage::Database::bind_text(stmt, 7, item.snippet);
            storage::Database::bind_text(stmt, 8, item.body_json);
            storage::Database::bind_int64(stmt, 9, item.round);
            storage::Database::bind_int64(stmt, 10, item.created_at);
        });
}

std::vector<EvidenceItem> EvidenceStore::list_evidence(const std::string& run_id) {
    std::vector<EvidenceItem> out;
    if (!db_) {
        return out;
    }
    auto rows = db_->query(
        "SELECT id, run_id, kind, title, source_uri, module_id, snippet, body_json, round, created_at "
        "FROM evidence WHERE run_id=? ORDER BY round ASC, created_at ASC",
        [&](sqlite3_stmt* stmt) { storage::Database::bind_text(stmt, 1, run_id); });
    out.reserve(rows.size());
    for (const auto& row : rows) {
        out.push_back(evidence_from_row(row));
    }
    return out;
}

std::optional<EvidenceItem> EvidenceStore::get_evidence(const std::string& evidence_id) {
    if (!db_) {
        return std::nullopt;
    }
    auto rows = db_->query(
        "SELECT id, run_id, kind, title, source_uri, module_id, snippet, body_json, round, created_at "
        "FROM evidence WHERE id=?",
        [&](sqlite3_stmt* stmt) { storage::Database::bind_text(stmt, 1, evidence_id); });
    if (rows.empty()) {
        return std::nullopt;
    }
    return evidence_from_row(rows[0]);
}

std::int64_t EvidenceStore::append_event(const std::string& run_id, const std::string& phase,
                                         const std::string& payload_json) {
    if (!db_) {
        throw std::runtime_error("EvidenceStore is not open");
    }
    const auto ts = utils::now_unix_seconds();
    db_->execute(
        "INSERT INTO run_events(run_id, seq, phase, payload_json, ts) "
        "VALUES(?, COALESCE((SELECT MAX(seq)+1 FROM run_events WHERE run_id=?), 1), ?, ?, ?)",
        [&](sqlite3_stmt* stmt) {
            storage::Database::bind_text(stmt, 1, run_id);
            storage::Database::bind_text(stmt, 2, run_id);
            storage::Database::bind_text(stmt, 3, phase);
            storage::Database::bind_text(stmt, 4, payload_json);
            storage::Database::bind_int64(stmt, 5, ts);
        });
    auto rows = db_->query("SELECT MAX(seq) FROM run_events WHERE run_id=?",
                           [&](sqlite3_stmt* stmt) { storage::Database::bind_text(stmt, 1, run_id); });
    if (!rows.empty() && rows[0].size() > 0 && rows[0][0]) {
        return std::stoll(*rows[0][0]);
    }
    return 1;
}

} // namespace xscope::research
