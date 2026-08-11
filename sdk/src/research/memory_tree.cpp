#include "xscope/research/memory_tree.hpp"

#include "sqlite3.h"
#include "xscope/utils/time.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

namespace xscope::research {
namespace {

MemoryBranch branch_from_row(const storage::Database::Row& row) {
    MemoryBranch b;
    b.id = row.size() > 0 ? row[0].value_or("") : "";
    b.project_id = row.size() > 1 ? row[1].value_or("") : "";
    b.parent_branch_id = row.size() > 2 ? row[2].value_or("") : "";
    b.title = row.size() > 3 ? row[3].value_or("") : "";
    b.stage = row.size() > 4 ? row[4].value_or("") : "";
    b.run_id = row.size() > 5 ? row[5].value_or("") : "";
    b.meta_json = row.size() > 6 ? row[6].value_or("") : "";
    b.created_at = row.size() > 7 && row[7] ? std::stoll(*row[7]) : 0;
    b.updated_at = row.size() > 8 && row[8] ? std::stoll(*row[8]) : 0;
    return b;
}

MemoryEntry entry_from_row(const storage::Database::Row& row) {
    MemoryEntry e;
    e.id = row.size() > 0 ? row[0].value_or("") : "";
    e.project_id = row.size() > 1 ? row[1].value_or("") : "";
    e.branch_id = row.size() > 2 ? row[2].value_or("") : "";
    e.parent_id = row.size() > 3 ? row[3].value_or("") : "";
    e.run_id = row.size() > 4 ? row[4].value_or("") : "";
    e.title = row.size() > 5 ? row[5].value_or("") : "";
    e.summary = row.size() > 6 ? row[6].value_or("") : "";
    e.body = row.size() > 7 ? row[7].value_or("") : "";
    e.kind = row.size() > 8 ? row[8].value_or("") : "";
    e.direction_id = row.size() > 9 ? row[9].value_or("") : "";
    e.depth_layer = row.size() > 10 && row[10] ? static_cast<int>(std::stoll(*row[10])) : 0;
    e.meta_json = row.size() > 11 ? row[11].value_or("") : "";
    e.created_at = row.size() > 12 && row[12] ? std::stoll(*row[12]) : 0;
    e.updated_at = row.size() > 13 && row[13] ? std::stoll(*row[13]) : 0;
    return e;
}

} // namespace

void MemoryTreeStore::open(storage::Database& db) {
    db_ = &db;
}

void MemoryTreeStore::close() noexcept {
    db_ = nullptr;
}

void MemoryTreeStore::upsert_branch(const MemoryBranch& branch) {
    if (!db_) {
        throw std::runtime_error("MemoryTreeStore is not open");
    }
    const auto now = utils::now_unix_seconds();
    const auto created = branch.created_at > 0 ? branch.created_at : now;
    const auto updated = branch.updated_at > 0 ? branch.updated_at : now;
    db_->execute(
        "INSERT INTO memory_branches(id, project_id, parent_branch_id, title, stage, run_id, meta_json, "
        "created_at, updated_at) VALUES(?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "parent_branch_id=excluded.parent_branch_id, title=excluded.title, stage=excluded.stage, "
        "run_id=excluded.run_id, meta_json=excluded.meta_json, updated_at=excluded.updated_at",
        [&](sqlite3_stmt* stmt) {
            storage::Database::bind_text(stmt, 1, branch.id);
            storage::Database::bind_text(stmt, 2, branch.project_id);
            storage::Database::bind_text(stmt, 3, branch.parent_branch_id);
            storage::Database::bind_text(stmt, 4, branch.title);
            storage::Database::bind_text(stmt, 5, branch.stage);
            storage::Database::bind_text(stmt, 6, branch.run_id);
            storage::Database::bind_text(stmt, 7, branch.meta_json);
            storage::Database::bind_int64(stmt, 8, created);
            storage::Database::bind_int64(stmt, 9, updated);
        });
}

bool MemoryTreeStore::delete_branch(const std::string& project_id, const std::string& branch_id) {
    if (!db_) {
        return false;
    }
    db_->execute("DELETE FROM memory_entries WHERE project_id=? AND branch_id=?", [&](sqlite3_stmt* stmt) {
        storage::Database::bind_text(stmt, 1, project_id);
        storage::Database::bind_text(stmt, 2, branch_id);
    });
    db_->execute("DELETE FROM memory_branches WHERE project_id=? AND id=?", [&](sqlite3_stmt* stmt) {
        storage::Database::bind_text(stmt, 1, project_id);
        storage::Database::bind_text(stmt, 2, branch_id);
    });
    return true;
}

std::optional<MemoryBranch> MemoryTreeStore::get_branch(const std::string& project_id,
                                                        const std::string& branch_id) {
    if (!db_) {
        return std::nullopt;
    }
    auto rows = db_->query(
        "SELECT id, project_id, parent_branch_id, title, stage, run_id, meta_json, created_at, updated_at "
        "FROM memory_branches WHERE project_id=? AND id=? LIMIT 1",
        [&](sqlite3_stmt* stmt) {
            storage::Database::bind_text(stmt, 1, project_id);
            storage::Database::bind_text(stmt, 2, branch_id);
        });
    if (rows.empty()) {
        return std::nullopt;
    }
    return branch_from_row(rows.front());
}

std::vector<MemoryBranch> MemoryTreeStore::list_branches(const std::string& project_id) {
    std::vector<MemoryBranch> out;
    if (!db_) {
        return out;
    }
    auto rows = db_->query(
        "SELECT id, project_id, parent_branch_id, title, stage, run_id, meta_json, created_at, updated_at "
        "FROM memory_branches WHERE project_id=? ORDER BY updated_at DESC",
        [&](sqlite3_stmt* stmt) { storage::Database::bind_text(stmt, 1, project_id); });
    for (const auto& row : rows) {
        out.push_back(branch_from_row(row));
    }
    return out;
}

void MemoryTreeStore::upsert_entry(const MemoryEntry& entry) {
    if (!db_) {
        throw std::runtime_error("MemoryTreeStore is not open");
    }
    const auto now = utils::now_unix_seconds();
    const auto created = entry.created_at > 0 ? entry.created_at : now;
    const auto updated = entry.updated_at > 0 ? entry.updated_at : now;
    db_->execute(
        "INSERT INTO memory_entries(id, project_id, branch_id, parent_id, run_id, title, summary, body, "
        "kind, direction_id, depth_layer, meta_json, created_at, updated_at) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "branch_id=excluded.branch_id, parent_id=excluded.parent_id, run_id=excluded.run_id, "
        "title=excluded.title, summary=excluded.summary, body=excluded.body, kind=excluded.kind, "
        "direction_id=excluded.direction_id, depth_layer=excluded.depth_layer, "
        "meta_json=excluded.meta_json, updated_at=excluded.updated_at",
        [&](sqlite3_stmt* stmt) {
            storage::Database::bind_text(stmt, 1, entry.id);
            storage::Database::bind_text(stmt, 2, entry.project_id);
            storage::Database::bind_text(stmt, 3, entry.branch_id);
            storage::Database::bind_text(stmt, 4, entry.parent_id);
            storage::Database::bind_text(stmt, 5, entry.run_id);
            storage::Database::bind_text(stmt, 6, entry.title);
            storage::Database::bind_text(stmt, 7, entry.summary);
            storage::Database::bind_text(stmt, 8, entry.body);
            storage::Database::bind_text(stmt, 9, entry.kind);
            storage::Database::bind_text(stmt, 10, entry.direction_id);
            storage::Database::bind_int64(stmt, 11, entry.depth_layer);
            storage::Database::bind_text(stmt, 12, entry.meta_json);
            storage::Database::bind_int64(stmt, 13, created);
            storage::Database::bind_int64(stmt, 14, updated);
        });
}

bool MemoryTreeStore::delete_entry(const std::string& project_id, const std::string& entry_id) {
    if (!db_) {
        return false;
    }
    db_->execute("DELETE FROM memory_entries WHERE project_id=? AND id=?", [&](sqlite3_stmt* stmt) {
        storage::Database::bind_text(stmt, 1, project_id);
        storage::Database::bind_text(stmt, 2, entry_id);
    });
    return true;
}

std::optional<MemoryEntry> MemoryTreeStore::get_entry(const std::string& project_id,
                                                      const std::string& entry_id) {
    if (!db_) {
        return std::nullopt;
    }
    auto rows = db_->query(
        "SELECT id, project_id, branch_id, parent_id, run_id, title, summary, body, kind, direction_id, "
        "depth_layer, meta_json, created_at, updated_at FROM memory_entries "
        "WHERE project_id=? AND id=? LIMIT 1",
        [&](sqlite3_stmt* stmt) {
            storage::Database::bind_text(stmt, 1, project_id);
            storage::Database::bind_text(stmt, 2, entry_id);
        });
    if (rows.empty()) {
        return std::nullopt;
    }
    return entry_from_row(rows.front());
}

std::vector<MemoryEntry> MemoryTreeStore::list_entries(const std::string& project_id,
                                                       const std::string& branch_id) {
    std::vector<MemoryEntry> out;
    if (!db_) {
        return out;
    }
    if (branch_id.empty()) {
        auto rows = db_->query(
            "SELECT id, project_id, branch_id, parent_id, run_id, title, summary, body, kind, direction_id, "
            "depth_layer, meta_json, created_at, updated_at FROM memory_entries "
            "WHERE project_id=? ORDER BY created_at ASC",
            [&](sqlite3_stmt* stmt) { storage::Database::bind_text(stmt, 1, project_id); });
        for (const auto& row : rows) {
            out.push_back(entry_from_row(row));
        }
        return out;
    }
    auto rows = db_->query(
        "SELECT id, project_id, branch_id, parent_id, run_id, title, summary, body, kind, direction_id, "
        "depth_layer, meta_json, created_at, updated_at FROM memory_entries "
        "WHERE project_id=? AND branch_id=? ORDER BY created_at ASC",
        [&](sqlite3_stmt* stmt) {
            storage::Database::bind_text(stmt, 1, project_id);
            storage::Database::bind_text(stmt, 2, branch_id);
        });
    for (const auto& row : rows) {
        out.push_back(entry_from_row(row));
    }
    return out;
}

utils::Json MemoryTreeStore::catalog_json(const std::string& project_id) {
    utils::Json::Array branches;
    for (const auto& b : list_branches(project_id)) {
        branches.push_back(utils::Json(utils::Json::Object{
            {"id", b.id},
            {"parent_branch_id", b.parent_branch_id},
            {"title", b.title},
            {"stage", b.stage},
            {"run_id", b.run_id},
        }));
    }
    utils::Json::Array entries;
    for (const auto& e : list_entries(project_id)) {
        entries.push_back(utils::Json(utils::Json::Object{
            {"id", e.id},
            {"branch_id", e.branch_id},
            {"parent_id", e.parent_id},
            {"title", e.title},
            {"summary", e.summary},
            {"kind", e.kind},
            {"direction_id", e.direction_id},
            {"depth_layer", static_cast<std::int64_t>(e.depth_layer)},
            // no body in catalog
        }));
    }
    return utils::Json(utils::Json::Object{
        {"project_id", project_id},
        {"kind", std::string("memory_catalog")},
        {"branches", std::move(branches)},
        {"entries", std::move(entries)},
        {"note", std::string("Directory only — use memory_get / memory_chain to load bodies")},
    });
}

utils::Json MemoryTreeStore::chain_json(const std::string& project_id, const std::string& entry_id) {
    auto tip = get_entry(project_id, entry_id);
    if (!tip) {
        return utils::Json(utils::Json::Object{{"error", std::string("entry not found")}});
    }
    std::unordered_map<std::string, MemoryEntry> by_id;
    for (auto& e : list_entries(project_id, tip->branch_id)) {
        by_id[e.id] = std::move(e);
    }
    std::vector<MemoryEntry> chain_rev;
    std::string cur = entry_id;
    for (int guard = 0; guard < 256 && !cur.empty(); ++guard) {
        auto it = by_id.find(cur);
        if (it == by_id.end()) {
            break;
        }
        chain_rev.push_back(it->second);
        cur = it->second.parent_id;
    }
    utils::Json::Array chain;
    for (auto it = chain_rev.rbegin(); it != chain_rev.rend(); ++it) {
        chain.push_back(utils::Json(utils::Json::Object{
            {"id", it->id},
            {"parent_id", it->parent_id},
            {"title", it->title},
            {"summary", it->summary},
            {"body", it->body},
            {"kind", it->kind},
            {"depth_layer", static_cast<std::int64_t>(it->depth_layer)},
        }));
    }
    return utils::Json(utils::Json::Object{
        {"project_id", project_id},
        {"branch_id", tip->branch_id},
        {"tip_id", entry_id},
        {"chain", std::move(chain)},
    });
}

utils::Json MemoryTreeStore::shallow_related_json(const std::string& project_id,
                                                  const std::string& branch_id, int limit) {
    utils::Json::Array recent;
    auto entries = list_entries(project_id, branch_id);
    const int n = static_cast<int>(entries.size());
    const int start = std::max(0, n - std::max(1, limit));
    for (int i = start; i < n; ++i) {
        recent.push_back(utils::Json(utils::Json::Object{
            {"id", entries[i].id},
            {"title", entries[i].title},
            {"summary", entries[i].summary},
            {"kind", entries[i].kind},
        }));
    }
    utils::Json::Array siblings;
    for (const auto& b : list_branches(project_id)) {
        if (b.id == branch_id) {
            continue;
        }
        siblings.push_back(utils::Json(utils::Json::Object{
            {"id", b.id},
            {"title", b.title},
            {"parent_branch_id", b.parent_branch_id},
        }));
    }
    return utils::Json(utils::Json::Object{
        {"branch_id", branch_id},
        {"recent_on_branch", std::move(recent)},
        {"sibling_branch_directory", std::move(siblings)},
    });
}

} // namespace xscope::research
