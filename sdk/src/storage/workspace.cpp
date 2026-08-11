#include "xscope/storage/workspace.hpp"

#include "sqlite3.h"
#include "xscope/ai/builtin.hpp"
#include "xscope/providers/github/builtin.hpp"
#include "xscope/providers/bocha/builtin.hpp"
#include "xscope/providers/bocha/client.hpp"
#include "xscope/providers/twtapi/builtin.hpp"
#include "xscope/providers/twtapi/client.hpp"
#include "xscope/utils/utils.hpp"
#include "xscope/xaiop/bridge.hpp"

#include <filesystem>
#include <random>
#include <sstream>

namespace fs = std::filesystem;

namespace xscope::storage {
namespace {

constexpr int kGlobalSchema = 2;
constexpr int kProjectSchema = 5;

void migrate_global(Database& db, int from, int to) {
    if (from < 1 && to >= 1) {
        db.exec(R"SQL(
            CREATE TABLE IF NOT EXISTS meta (
                key TEXT PRIMARY KEY NOT NULL,
                value TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS secrets (
                id TEXT PRIMARY KEY NOT NULL,
                provider TEXT NOT NULL,
                nonce BLOB NOT NULL,
                ciphertext BLOB NOT NULL,
                updated_at INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS project_index (
                id TEXT PRIMARY KEY NOT NULL,
                title TEXT NOT NULL DEFAULT '',
                created_at INTEGER NOT NULL,
                updated_at INTEGER NOT NULL,
                path_rel TEXT NOT NULL
            );
        )SQL");
    }
    if (from < 2 && to >= 2) {
        db.exec("ALTER TABLE project_index ADD COLUMN pinned INTEGER NOT NULL DEFAULT 0");
    }
}

void migrate_project(Database& db, int from, int to) {
    if (from < 1 && to >= 1) {
        db.exec(R"SQL(
            CREATE TABLE IF NOT EXISTS meta (
                key TEXT PRIMARY KEY NOT NULL,
                value TEXT NOT NULL
            );
        )SQL");
    }
    if (from < 2 && to >= 2) {
        db.exec(R"SQL(
            CREATE TABLE IF NOT EXISTS research_runs (
                id TEXT PRIMARY KEY NOT NULL,
                project_id TEXT NOT NULL,
                query TEXT NOT NULL,
                model_id TEXT NOT NULL DEFAULT '',
                precision INTEGER NOT NULL DEFAULT 1,
                status TEXT NOT NULL DEFAULT 'pending',
                search_rounds_done INTEGER NOT NULL DEFAULT 0,
                error TEXT NOT NULL DEFAULT '',
                summary TEXT NOT NULL DEFAULT '',
                waiting_prompt TEXT NOT NULL DEFAULT '',
                created_at INTEGER NOT NULL,
                updated_at INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS evidence (
                id TEXT PRIMARY KEY NOT NULL,
                run_id TEXT NOT NULL,
                kind TEXT NOT NULL DEFAULT 'web',
                title TEXT NOT NULL DEFAULT '',
                source_uri TEXT NOT NULL DEFAULT '',
                module_id TEXT NOT NULL DEFAULT '',
                snippet TEXT NOT NULL DEFAULT '',
                body_json TEXT NOT NULL DEFAULT '',
                round INTEGER NOT NULL DEFAULT 0,
                created_at INTEGER NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_evidence_run ON evidence(run_id);
            CREATE TABLE IF NOT EXISTS run_events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                run_id TEXT NOT NULL,
                seq INTEGER NOT NULL,
                phase TEXT NOT NULL,
                payload_json TEXT NOT NULL DEFAULT '',
                ts INTEGER NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_run_events_run ON run_events(run_id, seq);
        )SQL");
    }
    if (from < 3 && to >= 3) {
        db.exec(R"SQL(
            CREATE TABLE IF NOT EXISTS knowledge_nodes (
                id TEXT PRIMARY KEY NOT NULL,
                project_id TEXT NOT NULL,
                run_id TEXT NOT NULL DEFAULT '',
                title TEXT NOT NULL DEFAULT '',
                content TEXT NOT NULL DEFAULT '',
                kind TEXT NOT NULL DEFAULT 'fact',
                direction_id TEXT NOT NULL DEFAULT '',
                depth_layer INTEGER NOT NULL DEFAULT 0,
                valid INTEGER NOT NULL DEFAULT 1,
                meta_json TEXT NOT NULL DEFAULT '',
                created_at INTEGER NOT NULL,
                updated_at INTEGER NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_knowledge_nodes_project ON knowledge_nodes(project_id);
            CREATE TABLE IF NOT EXISTS knowledge_edges (
                id TEXT PRIMARY KEY NOT NULL,
                project_id TEXT NOT NULL,
                from_id TEXT NOT NULL,
                to_id TEXT NOT NULL,
                relation TEXT NOT NULL DEFAULT 'related',
                meta_json TEXT NOT NULL DEFAULT '',
                created_at INTEGER NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_knowledge_edges_project ON knowledge_edges(project_id);
        )SQL");
    }
    if (from < 4 && to >= 4) {
        db.exec(R"SQL(
            CREATE TABLE IF NOT EXISTS memory_branches (
                id TEXT PRIMARY KEY NOT NULL,
                project_id TEXT NOT NULL,
                parent_branch_id TEXT NOT NULL DEFAULT '',
                title TEXT NOT NULL DEFAULT '',
                stage TEXT NOT NULL DEFAULT 'research',
                run_id TEXT NOT NULL DEFAULT '',
                meta_json TEXT NOT NULL DEFAULT '',
                created_at INTEGER NOT NULL,
                updated_at INTEGER NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_memory_branches_project ON memory_branches(project_id);
            CREATE TABLE IF NOT EXISTS memory_entries (
                id TEXT PRIMARY KEY NOT NULL,
                project_id TEXT NOT NULL,
                branch_id TEXT NOT NULL,
                parent_id TEXT NOT NULL DEFAULT '',
                run_id TEXT NOT NULL DEFAULT '',
                title TEXT NOT NULL DEFAULT '',
                summary TEXT NOT NULL DEFAULT '',
                body TEXT NOT NULL DEFAULT '',
                kind TEXT NOT NULL DEFAULT 'note',
                direction_id TEXT NOT NULL DEFAULT '',
                depth_layer INTEGER NOT NULL DEFAULT 0,
                meta_json TEXT NOT NULL DEFAULT '',
                created_at INTEGER NOT NULL,
                updated_at INTEGER NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_memory_entries_project ON memory_entries(project_id);
            CREATE INDEX IF NOT EXISTS idx_memory_entries_branch ON memory_entries(project_id, branch_id);
        )SQL");
    }
    if (from < 5 && to >= 5) {
        db.exec(R"SQL(
            ALTER TABLE knowledge_nodes ADD COLUMN summary TEXT NOT NULL DEFAULT '';
            ALTER TABLE knowledge_nodes ADD COLUMN weight REAL NOT NULL DEFAULT 0.5;
        )SQL");
    }
}

} // namespace

std::int64_t Workspace::now_unix() { return xscope::utils::now_unix_seconds(); }

std::string Workspace::make_project_id() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<std::uint64_t> dist;
    std::ostringstream oss;
    oss << std::hex << dist(rng) << dist(rng);
    auto id = oss.str();
    if (id.size() > 32) {
        id.resize(32);
    }
    return id;
}

void Workspace::ensure_layout() {
    const auto root = xscope::utils::path_from_utf8(data_root_);
    xscope::utils::ensure_directory(root / "global");
    xscope::utils::ensure_directory(root / "projects");
    xscope::utils::ensure_directory(root / "skills");
    xscope::utils::ensure_directory(root / "registry");
    xscope::utils::ensure_directory(root / "prompts");
}

void Workspace::open_global() {
    const auto root = xscope::utils::path_from_utf8(data_root_);
    const auto db_path = root / "global" / "global.db";
    const auto key_path = root / "global" / "master.key";

    crypto::MasterKeyStore keys(key_path);
    master_key_ = keys.load_or_create();
    secret_box_ = std::make_unique<crypto::SecretBox>(master_key_);

    global_.open(db_path);
    global_.migrate(kGlobalSchema, migrate_global);
}

void Workspace::open(const std::string& data_root) {
    if (data_root.empty()) {
        throw DatabaseError("data_root is empty");
    }
    data_root_ = data_root;
    ensure_layout();
    open_global();
    const auto root = xscope::utils::path_from_utf8(data_root_);
    skills_.open(root / "skills");
    search_registry_.open(root / "registry" / "search_modules.json");
    ai_registry_.open(root / "registry" / "ai_providers.json");
    prompts_.open(root / "prompts");
    ensure_search_providers();
    ensure_ai_providers();
}

void Workspace::close() noexcept {
    prompts_.close();
    ai_registry_.close();
    search_registry_.close();
    skills_.close();
    global_.close();
    secret_box_.reset();
    master_key_.clear();
    data_root_.clear();
}

void Workspace::ensure_github_provider() {
    providers::github::ensure_github_search_module(skills_, search_registry_);
}

void Workspace::ensure_bocha_provider() {
    providers::bocha::ensure_bocha_search_module(skills_, search_registry_);
}

void Workspace::ensure_twtapi_provider() {
    providers::twtapi::ensure_twtapi_search_module(skills_, search_registry_);
}

void Workspace::ensure_search_providers() {
    ensure_github_provider();
    ensure_bocha_provider();
    ensure_twtapi_provider();
}

void Workspace::ensure_ai_providers() { ai::ensure_builtin_ai_providers(ai_registry_); }

providers::bocha::Client Workspace::bocha_client() {
    return providers::bocha::Client(http_, [this]() { return get_secret("bocha.default"); });
}

providers::twtapi::Client Workspace::twtapi_client() {
    return providers::twtapi::Client(http_, [this]() { return get_secret("twtapi.default"); });
}

mcp::SearchToolService Workspace::search_tools(bool require_secret_present) {
    mcp::SearchToolService tools(
        search_registry_, skills_,
        [this](const std::string& sid) {
            auto s = get_secret(sid);
            return s.has_value() && !s->empty();
        },
        [this](const std::string& sid) { return get_secret(sid); }, http_, github_oauth(),
        [this](const std::string& project_id) { return open_project_db(project_id); });
    tools.set_require_secret_present(require_secret_present);
    return tools;
}

ai::AiRuntime Workspace::ai_runtime() {
    return ai::AiRuntime(http_, ai_registry_,
                         [this](const std::string& id) { return get_secret(id); });
}

auth::GithubOAuth Workspace::github_oauth() {
    auto cfg = auth::load_github_oauth_config(data_root_);
    return auth::GithubOAuth(
        http_, std::move(cfg),
        [this](const std::string& id) { return get_secret(id); },
        [this](const std::string& id, const std::string& provider, const std::string& plaintext) {
            put_secret(id, provider, plaintext);
        },
        [this](const std::string& id) { remove_secret(id); });
}

providers::github::RestClient Workspace::github_rest() {
    return providers::github::RestClient(http_, [this]() {
        auto oauth = github_oauth();
        return oauth.access_token();
    });
}

std::vector<registry::UsableSearchModule> Workspace::list_usable_search_modules(
    bool require_secret_present) {
    return registry::list_usable_search_modules(
        search_registry_, skills_, require_secret_present,
        [this](const std::string& sid) { return get_secret(sid).has_value(); });
}

std::string Workspace::render_chat_system_prompt(bool require_secret_present) {
    prompts::PromptContext ctx;
    ctx.usable_modules = list_usable_search_modules(require_secret_present);
    return prompts_.render_chat_system(ctx);
}

namespace {

ProjectInfo project_from_row(const std::vector<std::optional<std::string>>& row) {
    ProjectInfo info;
    info.id = row.size() > 0 ? row[0].value_or("") : "";
    info.title = row.size() > 1 ? row[1].value_or("") : "";
    info.created_at = row.size() > 2 && row[2] ? std::stoll(*row[2]) : 0;
    info.updated_at = row.size() > 3 && row[3] ? std::stoll(*row[3]) : 0;
    info.path_rel = row.size() > 4 ? row[4].value_or("") : "";
    info.pinned = row.size() > 5 && row[5] && std::stoll(*row[5]) != 0;
    return info;
}

} // namespace

ProjectInfo Workspace::create_project(const std::string& title) {
    ProjectInfo info;
    info.id = make_project_id();
    info.title = title;
    info.created_at = now_unix();
    info.updated_at = info.created_at;
    info.path_rel = "projects/" + info.id;
    info.pinned = false;

    const auto project_dir = xscope::utils::path_from_utf8(data_root_) / "projects" / info.id;
    xscope::utils::ensure_directory(project_dir / "files");

    {
        Database project_db;
        project_db.open(project_dir / "project.db");
        project_db.migrate(kProjectSchema, migrate_project);
        project_db.close();
    }

    global_.execute(
        "INSERT INTO project_index(id, title, created_at, updated_at, path_rel, pinned) "
        "VALUES(?,?,?,?,?,0)",
        [&](sqlite3_stmt* stmt) {
            Database::bind_text(stmt, 1, info.id);
            Database::bind_text(stmt, 2, info.title);
            Database::bind_int64(stmt, 3, info.created_at);
            Database::bind_int64(stmt, 4, info.updated_at);
            Database::bind_text(stmt, 5, info.path_rel);
        });
    return info;
}

std::vector<ProjectInfo> Workspace::list_projects() {
    auto rows = global_.query(
        "SELECT id, title, created_at, updated_at, path_rel, pinned FROM project_index "
        "ORDER BY pinned DESC, updated_at DESC");
    std::vector<ProjectInfo> out;
    out.reserve(rows.size());
    for (const auto& row : rows) {
        out.push_back(project_from_row(row));
    }
    return out;
}

std::optional<ProjectInfo> Workspace::get_project(const std::string& id) {
    auto rows = global_.query(
        "SELECT id, title, created_at, updated_at, path_rel, pinned FROM project_index WHERE id=?",
        [&](sqlite3_stmt* stmt) { Database::bind_text(stmt, 1, id); });
    if (rows.empty()) {
        return std::nullopt;
    }
    return project_from_row(rows[0]);
}

void Workspace::touch_project(const std::string& id, const std::string& title) {
    global_.execute("UPDATE project_index SET title=?, updated_at=? WHERE id=?",
                    [&](sqlite3_stmt* stmt) {
                        Database::bind_text(stmt, 1, title);
                        Database::bind_int64(stmt, 2, now_unix());
                        Database::bind_text(stmt, 3, id);
                    });
}

void Workspace::rename_project(const std::string& id, const std::string& title) {
    if (!get_project(id)) {
        throw DatabaseError("unknown project id");
    }
    touch_project(id, title);
}

void Workspace::set_project_pinned(const std::string& id, bool pinned) {
    if (!get_project(id)) {
        throw DatabaseError("unknown project id");
    }
    global_.execute("UPDATE project_index SET pinned=? WHERE id=?", [&](sqlite3_stmt* stmt) {
        Database::bind_int64(stmt, 1, pinned ? 1 : 0);
        Database::bind_text(stmt, 2, id);
    });
}

void Workspace::delete_project(const std::string& id) {
    if (!get_project(id)) {
        throw DatabaseError("unknown project id");
    }
    global_.execute("DELETE FROM project_index WHERE id=?",
                    [&](sqlite3_stmt* stmt) { Database::bind_text(stmt, 1, id); });
    const auto dir = xscope::utils::path_from_utf8(data_root_) / "projects" / id;
    std::error_code ec;
    fs::remove_all(dir, ec);
}

Database Workspace::open_project_db(const std::string& id) {
    auto info = get_project(id);
    if (!info) {
        throw DatabaseError("unknown project id");
    }
    const auto db_path = xscope::utils::path_from_utf8(data_root_) / "projects" / id / "project.db";
    Database db;
    db.open(db_path);
    db.migrate(kProjectSchema, migrate_project);
    return db;
}

void Workspace::put_secret(const std::string& id, const std::string& provider,
                           const std::string& plaintext) {
    if (!secret_box_) {
        throw DatabaseError("workspace is not open");
    }
    auto sealed = secret_box_->seal_string(plaintext);
    global_.execute(
        "INSERT INTO secrets(id, provider, nonce, ciphertext, updated_at) VALUES(?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET provider=excluded.provider, nonce=excluded.nonce, "
        "ciphertext=excluded.ciphertext, updated_at=excluded.updated_at",
        [&](sqlite3_stmt* stmt) {
            Database::bind_text(stmt, 1, id);
            Database::bind_text(stmt, 2, provider);
            Database::bind_blob(stmt, 3, sealed.nonce.data(), static_cast<int>(sealed.nonce.size()));
            Database::bind_blob(stmt, 4, sealed.ciphertext.data(),
                                static_cast<int>(sealed.ciphertext.size()));
            Database::bind_int64(stmt, 5, now_unix());
        });
}

std::optional<std::string> Workspace::get_secret(const std::string& id) {
    if (!secret_box_) {
        throw DatabaseError("workspace is not open");
    }
    auto rows = global_.query("SELECT nonce, ciphertext FROM secrets WHERE id=?",
                              [&](sqlite3_stmt* stmt) { Database::bind_text(stmt, 1, id); });
    if (rows.empty()) {
        return std::nullopt;
    }
    crypto::SealedSecret sealed;
    const auto& nonce = *rows[0][0];
    const auto& ct = *rows[0][1];
    sealed.nonce.assign(nonce.begin(), nonce.end());
    sealed.ciphertext.assign(ct.begin(), ct.end());
    return secret_box_->open_string(sealed);
}

void Workspace::remove_secret(const std::string& id) {
    global_.execute("DELETE FROM secrets WHERE id=?",
                    [&](sqlite3_stmt* stmt) { Database::bind_text(stmt, 1, id); });
}

std::vector<std::string> Workspace::list_secret_ids() {
    auto rows = global_.query("SELECT id FROM secrets ORDER BY id");
    std::vector<std::string> out;
    for (const auto& row : rows) {
        out.push_back(row[0].value_or(""));
    }
    return out;
}

std::string Workspace::projects_history_xaiop() {
    // Keyed map under projects — suitable for later locate/update on the UI stream.
    std::ostringstream json;
    json << "{\"meta\":{\"kind\":\"project_history\",\"schema\":1},\"projects\":{";
    const auto projects = list_projects();
    for (size_t i = 0; i < projects.size(); ++i) {
        const auto& p = projects[i];
        if (i) {
            json << ',';
        }
        json << '"' << xscope::utils::json_escape(p.id) << "\":{"
             << "\"title\":\"" << xscope::utils::json_escape(p.title) << "\","
             << "\"created_at\":" << p.created_at << ','
             << "\"updated_at\":" << p.updated_at << ','
             << "\"pinned\":" << (p.pinned ? "true" : "false") << ','
             << "\"path_rel\":\"" << xscope::utils::json_escape(p.path_rel) << "\"}";
    }
    json << "}}";
    return xscope::xaiop::Bridge::instance().encode_json(json.str());
}

} // namespace xscope::storage
