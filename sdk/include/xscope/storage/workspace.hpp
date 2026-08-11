#pragma once

#include "xscope/ai/provider_registry.hpp"
#include "xscope/ai/runtime.hpp"
#include "xscope/auth/github_oauth.hpp"
#include "xscope/crypto/secret_box.hpp"
#include "xscope/mcp/search_tools.hpp"
#include "xscope/network/http_client.hpp"
#include "xscope/prompts/prompt_engine.hpp"
#include "xscope/providers/github/rest_client.hpp"
#include "xscope/providers/bocha/client.hpp"
#include "xscope/providers/twtapi/client.hpp"
#include "xscope/registry/search_registry.hpp"
#include "xscope/registry/usable_module.hpp"
#include "xscope/skills/skill_store.hpp"
#include "xscope/storage/database.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xscope::storage {

struct ProjectInfo {
    std::string id;
    std::string title;
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;
    std::string path_rel;
    bool pinned = false;
};

/// Owns data_root layout, global encrypted DB, and project DBs.
class Workspace {
public:
    /// @param data_root Client-provided safe directory (not chosen by SDK).
    void open(const std::string& data_root);
    void close() noexcept;

    const std::string& data_root() const noexcept { return data_root_; }
    Database& global_db() { return global_; }
    crypto::SecretBox& secrets() { return *secret_box_; }
    network::HttpClient& http() { return http_; }

    ProjectInfo create_project(const std::string& title);
    std::vector<ProjectInfo> list_projects();
    std::optional<ProjectInfo> get_project(const std::string& id);
    /// Rename + bump updated_at.
    void touch_project(const std::string& id, const std::string& title);
    void rename_project(const std::string& id, const std::string& title);
    void set_project_pinned(const std::string& id, bool pinned);
    void delete_project(const std::string& id);

    /// Open (create if needed) the per-project database under projects/<id>/.
    Database open_project_db(const std::string& id);

    void put_secret(const std::string& id, const std::string& provider, const std::string& plaintext);
    std::optional<std::string> get_secret(const std::string& id);
    void remove_secret(const std::string& id);
    std::vector<std::string> list_secret_ids();

    /// UI-bound history stream: XAIOP wire for research project index (Snapshot-friendly).
    std::string projects_history_xaiop();

    /// File-based skills under data_root/skills/.
    skills::SkillStore& skills() { return skills_; }
    const skills::SkillStore& skills() const { return skills_; }

    /// Search module registry JSON under data_root/registry/search_modules.json.
    registry::SearchRegistry& search_registry() { return search_registry_; }
    const registry::SearchRegistry& search_registry() const { return search_registry_; }

    /// AI provider/model registry JSON under data_root/registry/ai_providers.json.
    ai::ProviderRegistry& ai_registry() { return ai_registry_; }
    const ai::ProviderRegistry& ai_registry() const { return ai_registry_; }

    /// Prompt templates under data_root/prompts/.
    prompts::PromptEngine& prompts() { return prompts_; }
    const prompts::PromptEngine& prompts() const { return prompts_; }

    /// Full-fidelity usable search modules (complete skill text included).
    std::vector<registry::UsableSearchModule> list_usable_search_modules(
        bool require_secret_present = false);

    /// Render chat system prompt with mandatory full search-module injection.
    std::string render_chat_system_prompt(bool require_secret_present = false);

    /// MCP search / GitHub OAuth / run_search tools bound to this workspace.
    mcp::SearchToolService search_tools(bool require_secret_present = false);

    /// GitHub OAuth session (Device Flow + PAT fallback).
    auth::GithubOAuth github_oauth();

    /// Authenticated GitHub REST client using the stored github.oauth token.
    providers::github::RestClient github_rest();

    /// Ensure builtin github skill + registry module exist (idempotent).
    void ensure_github_provider();

    /// Ensure builtin Bocha skill + registry module exist (idempotent).
    void ensure_bocha_provider();

    /// Ensure builtin TwtAPI (Twitter/X) skill + registry module exist (idempotent).
    void ensure_twtapi_provider();

    /// Ensure all builtin search modules (GitHub, Bocha, TwtAPI, …).
    void ensure_search_providers();

    /// Ensure builtin DeepSeek / Kimi AI providers exist (idempotent; models via API sync).
    void ensure_ai_providers();

    /// AI runtime: catalog/chat/stream — **all returns are XAIOP wire**.
    ai::AiRuntime ai_runtime();

    /// Authenticated Bocha client using secret `bocha.default`.
    providers::bocha::Client bocha_client();

    /// Authenticated TwtAPI client using secret `twtapi.default`.
    providers::twtapi::Client twtapi_client();

private:
    void ensure_layout();
    void open_global();
    static std::int64_t now_unix();
    static std::string make_project_id();

    std::string data_root_;
    Database global_;
    std::vector<std::uint8_t> master_key_;
    std::unique_ptr<crypto::SecretBox> secret_box_;
    network::HttpClient http_;
    skills::SkillStore skills_;
    registry::SearchRegistry search_registry_;
    ai::ProviderRegistry ai_registry_;
    prompts::PromptEngine prompts_;
};

} // namespace xscope::storage
