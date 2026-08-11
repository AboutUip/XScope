#pragma once

#include "xscope/storage/database.hpp"
#include "xscope/utils/json.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xscope::research {

/// Radiating-tree branch (side-path), similar to a git branch for follow-ups.
struct MemoryBranch {
    std::string id;
    std::string project_id;
    std::string parent_branch_id; // empty = trunk / root path
    std::string title;
    std::string stage; // research | followup | …
    std::string run_id;
    std::string meta_json;
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;
};

/// One memory node on a branch chain (parent_id forms the chain within the branch).
struct MemoryEntry {
    std::string id;
    std::string project_id;
    std::string branch_id;
    std::string parent_id; // previous memory on this chain; empty = root of branch
    std::string run_id;
    std::string title;
    std::string summary; // short — used in catalogs
    std::string body;    // full — only via get
    std::string kind;    // note | finding | decision | ask | evidence_ref | …
    std::string direction_id;
    int depth_layer = 0;
    std::string meta_json;
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;
};

/// Project-scoped stage memory as a radiating tree of branches + chained entries.
class MemoryTreeStore {
public:
    void open(storage::Database& db);
    void close() noexcept;

    void upsert_branch(const MemoryBranch& branch);
    bool delete_branch(const std::string& project_id, const std::string& branch_id);
    std::optional<MemoryBranch> get_branch(const std::string& project_id, const std::string& branch_id);
    std::vector<MemoryBranch> list_branches(const std::string& project_id);

    void upsert_entry(const MemoryEntry& entry);
    bool delete_entry(const std::string& project_id, const std::string& entry_id);
    std::optional<MemoryEntry> get_entry(const std::string& project_id, const std::string& entry_id);
    std::vector<MemoryEntry> list_entries(const std::string& project_id, const std::string& branch_id = "");

    /// Directory only (no bodies): branches + entry titles/summaries.
    utils::Json catalog_json(const std::string& project_id);
    /// Full chain from root → entry on its branch (bodies included).
    utils::Json chain_json(const std::string& project_id, const std::string& entry_id);
    /// Shallow related: same branch recent entries (summaries) + optional sibling branches titles.
    utils::Json shallow_related_json(const std::string& project_id, const std::string& branch_id,
                                     int limit = 5);

private:
    storage::Database* db_ = nullptr;
};

} // namespace xscope::research
