#pragma once

#include "xscope/storage/database.hpp"
#include "xscope/utils/json.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xscope::research {

struct KnowledgeNode {
    std::string id;
    std::string project_id;
    std::string run_id;
    std::string title;
    std::string content;
    std::string summary; // AI-authored short synthesis (not just title/body)
    /// Importance in [0,1]. Use <0 as "unset" for partial updates.
    double weight = -1.0;
    std::string kind; // fact | entity | finding | code | note
    std::string direction_id;
    int depth_layer = 0;
    bool valid = true;
    std::string meta_json;
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;
};

struct KnowledgeEdge {
    std::string id;
    std::string project_id;
    std::string from_id;
    std::string to_id;
    std::string relation; // related | depends | explains | cites | part_of
    std::string meta_json;
    std::int64_t created_at = 0;
};

/// Project-scoped knowledge association graph (SQLite in project.db).
class KnowledgeGraphStore {
public:
    void open(storage::Database& db);
    void close() noexcept;

    void upsert_node(const KnowledgeNode& node);
    bool update_node(const KnowledgeNode& node);
    bool delete_node(const std::string& project_id, const std::string& node_id);
    std::optional<KnowledgeNode> get_node(const std::string& project_id, const std::string& node_id);
    std::vector<KnowledgeNode> list_nodes(const std::string& project_id);

    void upsert_edge(const KnowledgeEdge& edge);
    bool delete_edge(const std::string& project_id, const std::string& edge_id);
    /// Deletes edges matching endpoints; empty relation matches any. Returns count deleted.
    int delete_edges_between(const std::string& project_id, const std::string& from_id,
                             const std::string& to_id, const std::string& relation = "");
    std::vector<KnowledgeEdge> list_edges(const std::string& project_id);

    /// Full graph snapshot for MCP / prompts.
    utils::Json graph_json(const std::string& project_id);
    /// Directory only (ids/titles/kinds — no full content).
    utils::Json catalog_json(const std::string& project_id);

private:
    storage::Database* db_ = nullptr;
};

} // namespace xscope::research
