#include "xscope/research/knowledge_graph.hpp"

#include "sqlite3.h"
#include "xscope/utils/time.hpp"

#include <stdexcept>

namespace xscope::research {
namespace {

double clamp_weight(double w) {
    if (w < 0.0) {
        return 0.5;
    }
    if (w > 1.0) {
        return 1.0;
    }
    return w;
}

KnowledgeNode node_from_row(const storage::Database::Row& row) {
    KnowledgeNode n;
    n.id = row.size() > 0 ? row[0].value_or("") : "";
    n.project_id = row.size() > 1 ? row[1].value_or("") : "";
    n.run_id = row.size() > 2 ? row[2].value_or("") : "";
    n.title = row.size() > 3 ? row[3].value_or("") : "";
    n.content = row.size() > 4 ? row[4].value_or("") : "";
    n.summary = row.size() > 5 ? row[5].value_or("") : "";
    try {
        n.weight = row.size() > 6 && row[6] ? std::stod(*row[6]) : 0.5;
    } catch (...) {
        n.weight = 0.5;
    }
    n.kind = row.size() > 7 ? row[7].value_or("") : "";
    n.direction_id = row.size() > 8 ? row[8].value_or("") : "";
    n.depth_layer = row.size() > 9 && row[9] ? static_cast<int>(std::stoll(*row[9])) : 0;
    n.valid = row.size() > 10 && row[10] ? (std::stoll(*row[10]) != 0) : true;
    n.meta_json = row.size() > 11 ? row[11].value_or("") : "";
    n.created_at = row.size() > 12 && row[12] ? std::stoll(*row[12]) : 0;
    n.updated_at = row.size() > 13 && row[13] ? std::stoll(*row[13]) : 0;
    return n;
}

KnowledgeEdge edge_from_row(const storage::Database::Row& row) {
    KnowledgeEdge e;
    e.id = row.size() > 0 ? row[0].value_or("") : "";
    e.project_id = row.size() > 1 ? row[1].value_or("") : "";
    e.from_id = row.size() > 2 ? row[2].value_or("") : "";
    e.to_id = row.size() > 3 ? row[3].value_or("") : "";
    e.relation = row.size() > 4 ? row[4].value_or("") : "";
    e.meta_json = row.size() > 5 ? row[5].value_or("") : "";
    e.created_at = row.size() > 6 && row[6] ? std::stoll(*row[6]) : 0;
    return e;
}

} // namespace

void KnowledgeGraphStore::open(storage::Database& db) {
    db_ = &db;
}

void KnowledgeGraphStore::close() noexcept {
    db_ = nullptr;
}

void KnowledgeGraphStore::upsert_node(const KnowledgeNode& node) {
    if (!db_) {
        throw std::runtime_error("KnowledgeGraphStore is not open");
    }
    const auto now = utils::now_unix_seconds();
    const auto created = node.created_at > 0 ? node.created_at : now;
    const auto updated = node.updated_at > 0 ? node.updated_at : now;
    const double weight = clamp_weight(node.weight);
    db_->execute(
        "INSERT INTO knowledge_nodes(id, project_id, run_id, title, content, summary, weight, kind, "
        "direction_id, depth_layer, valid, meta_json, created_at, updated_at) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "run_id=excluded.run_id, title=excluded.title, content=excluded.content, "
        "summary=excluded.summary, weight=excluded.weight, kind=excluded.kind, "
        "direction_id=excluded.direction_id, depth_layer=excluded.depth_layer, valid=excluded.valid, "
        "meta_json=excluded.meta_json, updated_at=excluded.updated_at",
        [&](sqlite3_stmt* stmt) {
            storage::Database::bind_text(stmt, 1, node.id);
            storage::Database::bind_text(stmt, 2, node.project_id);
            storage::Database::bind_text(stmt, 3, node.run_id);
            storage::Database::bind_text(stmt, 4, node.title);
            storage::Database::bind_text(stmt, 5, node.content);
            storage::Database::bind_text(stmt, 6, node.summary);
            storage::Database::bind_double(stmt, 7, weight);
            storage::Database::bind_text(stmt, 8, node.kind);
            storage::Database::bind_text(stmt, 9, node.direction_id);
            storage::Database::bind_int64(stmt, 10, node.depth_layer);
            storage::Database::bind_int64(stmt, 11, node.valid ? 1 : 0);
            storage::Database::bind_text(stmt, 12, node.meta_json);
            storage::Database::bind_int64(stmt, 13, created);
            storage::Database::bind_int64(stmt, 14, updated);
        });
}

bool KnowledgeGraphStore::update_node(const KnowledgeNode& node) {
    auto existing = get_node(node.project_id, node.id);
    if (!existing) {
        return false;
    }
    KnowledgeNode n = *existing;
    if (!node.title.empty()) {
        n.title = node.title;
    }
    if (!node.content.empty()) {
        n.content = node.content;
    }
    if (!node.summary.empty()) {
        n.summary = node.summary;
    }
    if (node.weight >= 0.0) {
        n.weight = clamp_weight(node.weight);
    }
    if (!node.kind.empty()) {
        n.kind = node.kind;
    }
    if (!node.direction_id.empty()) {
        n.direction_id = node.direction_id;
    }
    if (node.depth_layer > 0) {
        n.depth_layer = node.depth_layer;
    }
    n.valid = node.valid;
    if (!node.meta_json.empty()) {
        n.meta_json = node.meta_json;
    }
    if (!node.run_id.empty()) {
        n.run_id = node.run_id;
    }
    n.updated_at = utils::now_unix_seconds();
    upsert_node(n);
    return true;
}

bool KnowledgeGraphStore::delete_node(const std::string& project_id, const std::string& node_id) {
    if (!db_) {
        return false;
    }
    db_->execute(
        "DELETE FROM knowledge_edges WHERE project_id=? AND (from_id=? OR to_id=?)",
        [&](sqlite3_stmt* stmt) {
            storage::Database::bind_text(stmt, 1, project_id);
            storage::Database::bind_text(stmt, 2, node_id);
            storage::Database::bind_text(stmt, 3, node_id);
        });
    db_->execute("DELETE FROM knowledge_nodes WHERE project_id=? AND id=?", [&](sqlite3_stmt* stmt) {
        storage::Database::bind_text(stmt, 1, project_id);
        storage::Database::bind_text(stmt, 2, node_id);
    });
    return true;
}

std::optional<KnowledgeNode> KnowledgeGraphStore::get_node(const std::string& project_id,
                                                           const std::string& node_id) {
    if (!db_) {
        return std::nullopt;
    }
    auto rows = db_->query(
        "SELECT id, project_id, run_id, title, content, summary, weight, kind, direction_id, "
        "depth_layer, valid, meta_json, created_at, updated_at FROM knowledge_nodes "
        "WHERE project_id=? AND id=? LIMIT 1",
        [&](sqlite3_stmt* stmt) {
            storage::Database::bind_text(stmt, 1, project_id);
            storage::Database::bind_text(stmt, 2, node_id);
        });
    if (rows.empty()) {
        return std::nullopt;
    }
    return node_from_row(rows.front());
}

std::vector<KnowledgeNode> KnowledgeGraphStore::list_nodes(const std::string& project_id) {
    std::vector<KnowledgeNode> out;
    if (!db_) {
        return out;
    }
    auto rows = db_->query(
        "SELECT id, project_id, run_id, title, content, summary, weight, kind, direction_id, "
        "depth_layer, valid, meta_json, created_at, updated_at FROM knowledge_nodes "
        "WHERE project_id=? ORDER BY updated_at DESC",
        [&](sqlite3_stmt* stmt) { storage::Database::bind_text(stmt, 1, project_id); });
    out.reserve(rows.size());
    for (const auto& row : rows) {
        out.push_back(node_from_row(row));
    }
    return out;
}

void KnowledgeGraphStore::upsert_edge(const KnowledgeEdge& edge) {
    if (!db_) {
        throw std::runtime_error("KnowledgeGraphStore is not open");
    }
    const auto created = edge.created_at > 0 ? edge.created_at : utils::now_unix_seconds();
    db_->execute(
        "INSERT INTO knowledge_edges(id, project_id, from_id, to_id, relation, meta_json, created_at) "
        "VALUES(?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "from_id=excluded.from_id, to_id=excluded.to_id, relation=excluded.relation, "
        "meta_json=excluded.meta_json",
        [&](sqlite3_stmt* stmt) {
            storage::Database::bind_text(stmt, 1, edge.id);
            storage::Database::bind_text(stmt, 2, edge.project_id);
            storage::Database::bind_text(stmt, 3, edge.from_id);
            storage::Database::bind_text(stmt, 4, edge.to_id);
            storage::Database::bind_text(stmt, 5, edge.relation);
            storage::Database::bind_text(stmt, 6, edge.meta_json);
            storage::Database::bind_int64(stmt, 7, created);
        });
}

bool KnowledgeGraphStore::delete_edge(const std::string& project_id, const std::string& edge_id) {
    if (!db_) {
        return false;
    }
    db_->execute("DELETE FROM knowledge_edges WHERE project_id=? AND id=?", [&](sqlite3_stmt* stmt) {
        storage::Database::bind_text(stmt, 1, project_id);
        storage::Database::bind_text(stmt, 2, edge_id);
    });
    return true;
}

int KnowledgeGraphStore::delete_edges_between(const std::string& project_id,
                                              const std::string& from_id,
                                              const std::string& to_id,
                                              const std::string& relation) {
    if (!db_ || from_id.empty() || to_id.empty()) {
        return 0;
    }
    int deleted = 0;
    for (const auto& e : list_edges(project_id)) {
        if (e.from_id != from_id || e.to_id != to_id) {
            continue;
        }
        if (!relation.empty() && e.relation != relation) {
            continue;
        }
        if (delete_edge(project_id, e.id)) {
            deleted += 1;
        }
    }
    return deleted;
}

std::vector<KnowledgeEdge> KnowledgeGraphStore::list_edges(const std::string& project_id) {
    std::vector<KnowledgeEdge> out;
    if (!db_) {
        return out;
    }
    auto rows = db_->query(
        "SELECT id, project_id, from_id, to_id, relation, meta_json, created_at "
        "FROM knowledge_edges WHERE project_id=? ORDER BY created_at DESC",
        [&](sqlite3_stmt* stmt) { storage::Database::bind_text(stmt, 1, project_id); });
    out.reserve(rows.size());
    for (const auto& row : rows) {
        out.push_back(edge_from_row(row));
    }
    return out;
}

utils::Json KnowledgeGraphStore::graph_json(const std::string& project_id) {
    utils::Json::Array nodes;
    for (const auto& n : list_nodes(project_id)) {
        nodes.push_back(utils::Json(utils::Json::Object{
            {"id", n.id},
            {"title", n.title},
            {"content", n.content},
            {"summary", n.summary},
            {"weight", clamp_weight(n.weight)},
            {"kind", n.kind},
            {"direction_id", n.direction_id},
            {"depth_layer", static_cast<std::int64_t>(n.depth_layer)},
            {"valid", n.valid},
            {"run_id", n.run_id},
            {"meta_json", n.meta_json},
        }));
    }
    utils::Json::Array edges;
    for (const auto& e : list_edges(project_id)) {
        edges.push_back(utils::Json(utils::Json::Object{
            {"id", e.id},
            {"from_id", e.from_id},
            {"to_id", e.to_id},
            {"relation", e.relation},
            {"meta_json", e.meta_json},
        }));
    }
    // Capture sizes BEFORE move — moved-from vector::size() is 0.
    const auto node_count = static_cast<std::int64_t>(nodes.size());
    const auto edge_count = static_cast<std::int64_t>(edges.size());
    return utils::Json(utils::Json::Object{
        {"project_id", project_id},
        {"nodes", std::move(nodes)},
        {"edges", std::move(edges)},
        {"node_count", node_count},
        {"edge_count", edge_count},
    });
}

utils::Json KnowledgeGraphStore::catalog_json(const std::string& project_id) {
    utils::Json::Array nodes;
    for (const auto& n : list_nodes(project_id)) {
        nodes.push_back(utils::Json(utils::Json::Object{
            {"id", n.id},
            {"title", n.title},
            {"summary", n.summary},
            {"weight", clamp_weight(n.weight)},
            {"kind", n.kind},
            {"direction_id", n.direction_id},
            {"depth_layer", static_cast<std::int64_t>(n.depth_layer)},
            {"valid", n.valid},
            // no content in catalog
        }));
    }
    utils::Json::Array edges;
    for (const auto& e : list_edges(project_id)) {
        edges.push_back(utils::Json(utils::Json::Object{
            {"id", e.id},
            {"from_id", e.from_id},
            {"to_id", e.to_id},
            {"relation", e.relation},
        }));
    }
    return utils::Json(utils::Json::Object{
        {"project_id", project_id},
        {"kind", std::string("knowledge_catalog")},
        {"nodes", std::move(nodes)},
        {"edges", std::move(edges)},
        {"note", std::string("Directory only — use knowledge_graph_get or node id reads for full content")},
    });
}

} // namespace xscope::research
