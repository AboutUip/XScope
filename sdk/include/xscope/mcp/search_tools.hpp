#pragma once

#include "xscope/auth/github_oauth.hpp"
#include "xscope/mcp/tool_types.hpp"
#include "xscope/network/http_client.hpp"
#include "xscope/providers/bocha/client.hpp"
#include "xscope/providers/github/rest_client.hpp"
#include "xscope/registry/search_registry.hpp"
#include "xscope/registry/usable_module.hpp"
#include "xscope/skills/skill_store.hpp"
#include "xscope/storage/database.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace xscope::mcp {

/// MCP-facing tools for search modules, GitHub OAuth, Bocha, GitHub REST, and knowledge graph.
class SearchToolService {
public:
    using GetSecretFn = std::function<std::optional<std::string>(const std::string& secret_id)>;
    using OpenProjectDbFn = std::function<storage::Database(const std::string& project_id)>;

    static constexpr const char* kListSearchModules = "list_search_modules";
    static constexpr const char* kGetSearchModuleSkill = "get_search_module_skill";
    static constexpr const char* kGithubOAuthStart = "github_oauth_start";
    static constexpr const char* kGithubOAuthPoll = "github_oauth_poll";
    static constexpr const char* kGithubOAuthStatus = "github_oauth_status";
    static constexpr const char* kGithubOAuthDisconnect = "github_oauth_disconnect";
    static constexpr const char* kGithubOAuthSetPat = "github_oauth_set_pat";
    static constexpr const char* kRunSearch = "run_search";
    static constexpr const char* kGithubRestGet = "github_rest_get";
    static constexpr const char* kGithubRest = "github_rest";
    static constexpr const char* kGithubRestPaginate = "github_rest_paginate";
    static constexpr const char* kGithubResource = "github_resource";
    static constexpr const char* kGithubRestCatalog = "github_rest_catalog";
    static constexpr const char* kKnowledgeGraphGet = "knowledge_graph_get";
    static constexpr const char* kKnowledgeGraphAdd = "knowledge_graph_add";
    static constexpr const char* kKnowledgeGraphUpdate = "knowledge_graph_update";
    static constexpr const char* kKnowledgeGraphDelete = "knowledge_graph_delete";
    static constexpr const char* kKnowledgeGraphLink = "knowledge_graph_link";
    static constexpr const char* kKnowledgeGraphCatalog = "knowledge_graph_catalog";
    static constexpr const char* kMemoryCatalog = "memory_catalog";
    static constexpr const char* kMemoryGet = "memory_get";
    static constexpr const char* kMemoryChain = "memory_chain";
    static constexpr const char* kMemoryAdd = "memory_add";
    static constexpr const char* kMemoryBranchCreate = "memory_branch_create";
    static constexpr const char* kMemoryBranchList = "memory_branch_list";

    SearchToolService(registry::SearchRegistry& registry, skills::SkillStore& skills,
                      std::function<bool(const std::string& secret_id)> has_secret,
                      GetSecretFn get_secret, network::HttpClient& http,
                      auth::GithubOAuth github_oauth, OpenProjectDbFn open_project_db = {});

    void set_require_secret_present(bool require) { require_secret_present_ = require; }
    bool require_secret_present() const noexcept { return require_secret_present_; }

    auth::GithubOAuth& github_oauth() { return github_oauth_; }
    const auth::GithubOAuth& github_oauth() const { return github_oauth_; }

    std::vector<ToolDescriptor> descriptors() const;

    ToolResponse call(const ToolRequest& request);

    utils::Json list_search_modules_json() const;
    utils::Json get_search_module_skill_json(const std::string& module_id) const;

private:
    registry::UsableSearchModule require_usable(const std::string& module_id) const;
    providers::github::RestClient make_rest() const;
    providers::bocha::Client make_bocha() const;
    providers::github::ResponseJsonOptions enrich_opts(const utils::Json& args) const;
    bool require_github_connected(ToolResponse& resp) const;
    bool require_bocha_key(ToolResponse& resp) const;

    ToolResponse run_search(const utils::Json& args);
    ToolResponse run_github_search(const utils::Json& args);
    ToolResponse run_bocha_search(const utils::Json& args);
    ToolResponse github_rest(const utils::Json& args);
    ToolResponse github_rest_paginate(const utils::Json& args);
    ToolResponse github_resource(const utils::Json& args);
    ToolResponse knowledge_graph_get(const utils::Json& args);
    ToolResponse knowledge_graph_add(const utils::Json& args);
    ToolResponse knowledge_graph_update(const utils::Json& args);
    ToolResponse knowledge_graph_delete(const utils::Json& args);
    ToolResponse knowledge_graph_link(const utils::Json& args);
    ToolResponse knowledge_graph_catalog(const utils::Json& args);
    ToolResponse memory_catalog(const utils::Json& args);
    ToolResponse memory_get(const utils::Json& args);
    ToolResponse memory_chain(const utils::Json& args);
    ToolResponse memory_add(const utils::Json& args);
    ToolResponse memory_branch_create(const utils::Json& args);
    ToolResponse memory_branch_list(const utils::Json& args);

    registry::SearchRegistry& registry_;
    skills::SkillStore& skills_;
    std::function<bool(const std::string& secret_id)> has_secret_;
    GetSecretFn get_secret_;
    network::HttpClient& http_;
    auth::GithubOAuth github_oauth_;
    OpenProjectDbFn open_project_db_;
    bool require_secret_present_ = false;
};

} // namespace xscope::mcp
