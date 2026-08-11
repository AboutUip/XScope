#include "xscope/mcp/search_tools.hpp"

#include "xscope/registry/usable_module.hpp"
#include "xscope/research/knowledge_graph.hpp"
#include "xscope/research/memory_tree.hpp"

#include <algorithm>
#include <random>
#include <sstream>

namespace xscope::mcp {
namespace {

utils::Json::Object object_schema(utils::Json::Object properties, utils::Json::Array required,
                                  bool additional = false) {
    utils::Json::Object schema;
    schema.emplace("type", std::string("object"));
    schema.emplace("properties", utils::Json(std::move(properties)));
    if (!required.empty()) {
        schema.emplace("required", utils::Json(std::move(required)));
    }
    schema.emplace("additionalProperties", additional);
    return schema;
}

std::string poll_status_name(auth::PollStatus s) {
    switch (s) {
    case auth::PollStatus::Pending:
        return "authorization_pending";
    case auth::PollStatus::SlowDown:
        return "slow_down";
    case auth::PollStatus::Authorized:
        return "authorized";
    case auth::PollStatus::Expired:
        return "expired_token";
    case auth::PollStatus::AccessDenied:
        return "access_denied";
    case auth::PollStatus::Error:
        return "error";
    }
    return "error";
}

std::vector<std::pair<std::string, std::string>> query_object(const utils::Json& args) {
    std::vector<std::pair<std::string, std::string>> query;
    if (!args.contains("query") || !args.at("query").is_object()) {
        return query;
    }
    for (const auto& [k, v] : args.at("query").as_object()) {
        if (v.is_string()) {
            query.emplace_back(k, v.as_string());
        } else if (v.is_number()) {
            query.emplace_back(k, std::to_string(v.as_int64(0)));
        } else if (v.is_bool()) {
            query.emplace_back(k, v.as_bool() ? "true" : "false");
        } else {
            query.emplace_back(k, v.dump(0));
        }
    }
    return query;
}

} // namespace

SearchToolService::SearchToolService(registry::SearchRegistry& registry, skills::SkillStore& skills,
                                     std::function<bool(const std::string& secret_id)> has_secret,
                                     GetSecretFn get_secret, network::HttpClient& http,
                                     auth::GithubOAuth github_oauth, OpenProjectDbFn open_project_db)
    : registry_(registry), skills_(skills), has_secret_(std::move(has_secret)),
      get_secret_(std::move(get_secret)), http_(http), github_oauth_(std::move(github_oauth)),
      open_project_db_(std::move(open_project_db)) {}

providers::github::RestClient SearchToolService::make_rest() const {
    return providers::github::RestClient(http_, [this]() { return github_oauth_.access_token(); });
}

providers::bocha::Client SearchToolService::make_bocha() const {
    return providers::bocha::Client(http_, [this]() {
        if (!get_secret_) {
            return std::optional<std::string>{};
        }
        return get_secret_("bocha.default");
    });
}

providers::github::ResponseJsonOptions SearchToolService::enrich_opts(const utils::Json& args) const {
    providers::github::ResponseJsonOptions opts;
    opts.decode_base64_content =
        args.contains("decode_content") ? args.at("decode_content").as_bool(true) : false;
    return opts;
}

bool SearchToolService::require_github_connected(ToolResponse& resp) const {
    if (!github_oauth_.connected()) {
        resp.error = "GitHub is not connected; call github_oauth_start/poll or github_oauth_set_pat";
        return false;
    }
    return true;
}

bool SearchToolService::require_bocha_key(ToolResponse& resp) const {
    if (!get_secret_) {
        resp.error = "Bocha secret lookup is not configured";
        return false;
    }
    auto key = get_secret_("bocha.default");
    if (!key || key->empty()) {
        resp.error = "Bocha API key missing; store secret bocha.default via settings or "
                     "xscope_search_module_set_api_key";
        return false;
    }
    return true;
}

std::vector<ToolDescriptor> SearchToolService::descriptors() const {
    utils::Json::Object skill_props;
    skill_props.emplace("module_id", utils::Json::parse(R"({"type":"string"})"));

    utils::Json::Object start_props;
    start_props.emplace("scope", utils::Json::parse(
                                     R"({"type":"string","description":"Optional OAuth scope override"})"));
    start_props.emplace("open_browser",
                        utils::Json::parse(R"({"type":"boolean","description":"Open verification URL"})"));

    utils::Json::Object poll_props;
    poll_props.emplace("device_code", utils::Json::parse(R"({"type":"string"})"));

    utils::Json::Object pat_props;
    pat_props.emplace("token", utils::Json::parse(R"({"type":"string"})"));
    pat_props.emplace("scope", utils::Json::parse(R"({"type":"string"})"));

    utils::Json::Object run_props;
    run_props.emplace(
        "module_id",
        utils::Json::parse(R"JS({"type":"string","description":"github | bocha"})JS"));
    run_props.emplace(
        "endpoint",
        utils::Json::parse(
            R"JS({"type":"string","description":"github: repositories|code|issues|commits|users|topics|labels ; bocha: web-search|ai-search"})JS"));
    run_props.emplace(
        "q", utils::Json::parse(R"JS({"type":"string","description":"Search query; Bocha also accepts query"})JS"));
    run_props.emplace("query", utils::Json::parse(R"JS({"type":"string","description":"Bocha query alias of q"})JS"));
    run_props.emplace("sort", utils::Json::parse(R"JS({"type":"string"})JS"));
    run_props.emplace("order", utils::Json::parse(R"JS({"type":"string"})JS"));
    run_props.emplace("page", utils::Json::parse(R"JS({"type":"integer"})JS"));
    run_props.emplace("per_page", utils::Json::parse(R"JS({"type":"integer"})JS"));
    run_props.emplace("text_match", utils::Json::parse(R"JS({"type":"boolean"})JS"));
    run_props.emplace("search_type",
                      utils::Json::parse(R"JS({"type":"string","description":"issues only: semantic|hybrid"})JS"));
    run_props.emplace("advanced_search", utils::Json::parse(R"JS({"type":"string"})JS"));
    run_props.emplace("freshness", utils::Json::parse(R"JS({"type":"string","description":"Bocha freshness filter"})JS"));
    run_props.emplace("count", utils::Json::parse(R"JS({"type":"integer","description":"Bocha result count 1-50"})JS"));
    run_props.emplace("summary", utils::Json::parse(R"JS({"type":"boolean","description":"Bocha web-search summaries"})JS"));
    run_props.emplace("answer", utils::Json::parse(R"JS({"type":"boolean","description":"Bocha ai-search LLM answer"})JS"));
    run_props.emplace("stream", utils::Json::parse(R"JS({"type":"boolean","description":"Bocha ai-search streaming"})JS"));

    utils::Json::Object rest_props;
    rest_props.emplace("method", utils::Json::parse(R"({"type":"string","description":"get|head|post|put|patch|delete"})"));
    rest_props.emplace("path", utils::Json::parse(R"({"type":"string","description":"API path starting with /"})"));
    rest_props.emplace("query", utils::Json::parse(R"({"type":"object"})"));
    rest_props.emplace("body", utils::Json::parse(R"({"type":"string","description":"Raw JSON body for write methods"})"));
    rest_props.emplace("accept", utils::Json::parse(
                                     R"({"type":"string","description":"Override Accept, e.g. application/vnd.github.raw"})"));
    rest_props.emplace("text_match", utils::Json::parse(R"({"type":"boolean"})"));
    rest_props.emplace("decode_content",
                       utils::Json::parse(R"({"type":"boolean","description":"Decode base64 contents/blob into decoded_content"})"));

    utils::Json::Object page_props;
    page_props.emplace("path", utils::Json::parse(R"({"type":"string"})"));
    page_props.emplace("query", utils::Json::parse(R"({"type":"object"})"));
    page_props.emplace("per_page", utils::Json::parse(R"({"type":"integer"})"));
    page_props.emplace("max_pages", utils::Json::parse(R"({"type":"integer"})"));
    page_props.emplace("accept", utils::Json::parse(R"({"type":"string"})"));
    page_props.emplace("decode_content", utils::Json::parse(R"({"type":"boolean"})"));

    utils::Json::Object resource_props;
    resource_props.emplace(
        "resource",
        utils::Json::parse(
            R"({"type":"string","description":"Named resource id from github_rest_catalog"})"));
    resource_props.emplace("owner", utils::Json::parse(R"({"type":"string"})"));
    resource_props.emplace("repo", utils::Json::parse(R"({"type":"string"})"));
    resource_props.emplace("path", utils::Json::parse(R"({"type":"string"})"));
    resource_props.emplace("ref", utils::Json::parse(R"({"type":"string"})"));
    resource_props.emplace("number", utils::Json::parse(R"({"type":"integer"})"));
    resource_props.emplace("paginate", utils::Json::parse(R"({"type":"boolean"})"));
    resource_props.emplace("per_page", utils::Json::parse(R"({"type":"integer"})"));
    resource_props.emplace("max_pages", utils::Json::parse(R"({"type":"integer"})"));
    resource_props.emplace("decode_content", utils::Json::parse(R"({"type":"boolean"})"));
    resource_props.emplace("query", utils::Json::parse(R"({"type":"object"})"));

    return {
        ToolDescriptor{kListSearchModules,
                       "List usable search modules with COMPLETE metadata and COMPLETE SKILL.md text.",
                       utils::Json(object_schema({}, {}, false))},
        ToolDescriptor{kGetSearchModuleSkill,
                       "Return one search module with COMPLETE skill file by internal id.",
                       utils::Json(object_schema(std::move(skill_props), utils::Json::Array{std::string("module_id")}))},
        ToolDescriptor{kGithubOAuthStart,
                       "Start GitHub OAuth device flow. Returns user_code and verification_uri.",
                       utils::Json(object_schema(std::move(start_props), {}, false))},
        ToolDescriptor{kGithubOAuthPoll,
                       "Poll GitHub OAuth device flow once. On success stores encrypted token.",
                       utils::Json(object_schema(std::move(poll_props), utils::Json::Array{std::string("device_code")}))},
        ToolDescriptor{kGithubOAuthStatus, "GitHub connection status (never returns access_token).",
                       utils::Json(object_schema({}, {}, false))},
        ToolDescriptor{kGithubOAuthDisconnect, "Disconnect GitHub (delete stored OAuth/PAT secret).",
                       utils::Json(object_schema({}, {}, false))},
        ToolDescriptor{kGithubOAuthSetPat, "Fallback: store a GitHub PAT/token as github.oauth.",
                       utils::Json(object_schema(std::move(pat_props), utils::Json::Array{std::string("token")}))},
        ToolDescriptor{kRunSearch,
                       "Run a search module: GitHub REST search (module_id=github) or Bocha web/ai-search "
                       "(module_id=bocha). COMPLETE response body, no truncation.",
                       utils::Json(object_schema(std::move(run_props),
                                                 utils::Json::Array{std::string("module_id"),
                                                                    std::string("endpoint")},
                                                 true))},
        ToolDescriptor{kGithubRestGet,
                       "Alias of github_rest with method=GET. COMPLETE body.",
                       utils::Json(object_schema(rest_props, utils::Json::Array{std::string("path")}))},
        ToolDescriptor{kGithubRest,
                       "Authenticated GitHub REST call (any method/path/Accept). COMPLETE body for research.",
                       utils::Json(object_schema(std::move(rest_props), utils::Json::Array{std::string("path")}))},
        ToolDescriptor{kGithubRestPaginate,
                       "GET path and follow Link rel=next; keeps EVERY page body complete; combines JSON arrays into items.",
                       utils::Json(object_schema(std::move(page_props), utils::Json::Array{std::string("path")}))},
        ToolDescriptor{kGithubResource,
                       "Named GitHub REST resource helper (repo/contents/issues/pulls/git/...). Optional paginate + decode_content.",
                       utils::Json(object_schema(std::move(resource_props),
                                                 utils::Json::Array{std::string("resource")}, true))},
        ToolDescriptor{kGithubRestCatalog,
                       "Catalog of named GitHub REST resources supported by github_resource.",
                       utils::Json(object_schema({}, {}, false))},
        ToolDescriptor{kKnowledgeGraphGet,
                       "View the current project knowledge association graph (nodes + edges).",
                       utils::Json(object_schema(
                           utils::Json::Object{
                               {"project_id", utils::Json::parse(R"({"type":"string"})")},
                           },
                           utils::Json::Array{std::string("project_id")}))},
        ToolDescriptor{kKnowledgeGraphAdd,
                       "Add a knowledge node. Only call when the knowledge is valid (model-judged).",
                       utils::Json(object_schema(
                           utils::Json::Object{
                               {"project_id", utils::Json::parse(R"({"type":"string"})")},
                               {"id", utils::Json::parse(R"({"type":"string"})")},
                               {"title", utils::Json::parse(R"({"type":"string"})")},
                               {"content", utils::Json::parse(R"({"type":"string"})")},
                               {"kind", utils::Json::parse(R"({"type":"string"})")},
                               {"direction_id", utils::Json::parse(R"({"type":"string"})")},
                               {"depth_layer", utils::Json::parse(R"({"type":"integer"})")},
                               {"valid", utils::Json::parse(R"({"type":"boolean"})")},
                               {"run_id", utils::Json::parse(R"({"type":"string"})")},
                               {"meta_json", utils::Json::parse(R"({"type":"string"})")},
                           },
                           utils::Json::Array{std::string("project_id"), std::string("title")}, true))},
        ToolDescriptor{kKnowledgeGraphUpdate,
                       "Update an existing knowledge node in the project graph.",
                       utils::Json(object_schema(
                           utils::Json::Object{
                               {"project_id", utils::Json::parse(R"({"type":"string"})")},
                               {"id", utils::Json::parse(R"({"type":"string"})")},
                               {"title", utils::Json::parse(R"({"type":"string"})")},
                               {"content", utils::Json::parse(R"({"type":"string"})")},
                               {"kind", utils::Json::parse(R"({"type":"string"})")},
                               {"valid", utils::Json::parse(R"({"type":"boolean"})")},
                           },
                           utils::Json::Array{std::string("project_id"), std::string("id")}, true))},
        ToolDescriptor{kKnowledgeGraphDelete,
                       "Delete a knowledge node (and incident edges) or an edge by id.",
                       utils::Json(object_schema(
                           utils::Json::Object{
                               {"project_id", utils::Json::parse(R"({"type":"string"})")},
                               {"node_id", utils::Json::parse(R"({"type":"string"})")},
                               {"edge_id", utils::Json::parse(R"({"type":"string"})")},
                           },
                           utils::Json::Array{std::string("project_id")}))},
        ToolDescriptor{kKnowledgeGraphLink,
                       "Create an association edge between two knowledge nodes.",
                       utils::Json(object_schema(
                           utils::Json::Object{
                               {"project_id", utils::Json::parse(R"({"type":"string"})")},
                               {"from_id", utils::Json::parse(R"({"type":"string"})")},
                               {"to_id", utils::Json::parse(R"({"type":"string"})")},
                               {"relation", utils::Json::parse(R"({"type":"string"})")},
                               {"id", utils::Json::parse(R"({"type":"string"})")},
                           },
                           utils::Json::Array{std::string("project_id"), std::string("from_id"),
                                             std::string("to_id")}))},
        ToolDescriptor{kKnowledgeGraphCatalog,
                       "Knowledge association TABLE DIRECTORY (ids/titles/relations only, no bodies).",
                       utils::Json(object_schema(
                           utils::Json::Object{
                               {"project_id", utils::Json::parse(R"({"type":"string"})")},
                           },
                           utils::Json::Array{std::string("project_id")}))},
        ToolDescriptor{kMemoryCatalog,
                       "Stage memory DIRECTORY for the radiating-tree branches + entry summaries (no bodies).",
                       utils::Json(object_schema(
                           utils::Json::Object{
                               {"project_id", utils::Json::parse(R"({"type":"string"})")},
                           },
                           utils::Json::Array{std::string("project_id")}))},
        ToolDescriptor{kMemoryGet,
                       "Load one memory entry body by id.",
                       utils::Json(object_schema(
                           utils::Json::Object{
                               {"project_id", utils::Json::parse(R"({"type":"string"})")},
                               {"id", utils::Json::parse(R"({"type":"string"})")},
                           },
                           utils::Json::Array{std::string("project_id"), std::string("id")}))},
        ToolDescriptor{kMemoryChain,
                       "Load the full chain (root→tip) on a branch for one tip entry id.",
                       utils::Json(object_schema(
                           utils::Json::Object{
                               {"project_id", utils::Json::parse(R"({"type":"string"})")},
                               {"id", utils::Json::parse(R"({"type":"string"})")},
                           },
                           utils::Json::Array{std::string("project_id"), std::string("id")}))},
        ToolDescriptor{kMemoryAdd,
                       "Append a stage memory entry on a branch (radiating-tree chain).",
                       utils::Json(object_schema(
                           utils::Json::Object{
                               {"project_id", utils::Json::parse(R"({"type":"string"})")},
                               {"branch_id", utils::Json::parse(R"({"type":"string"})")},
                               {"parent_id", utils::Json::parse(R"({"type":"string"})")},
                               {"title", utils::Json::parse(R"({"type":"string"})")},
                               {"summary", utils::Json::parse(R"({"type":"string"})")},
                               {"body", utils::Json::parse(R"({"type":"string"})")},
                               {"kind", utils::Json::parse(R"({"type":"string"})")},
                               {"direction_id", utils::Json::parse(R"({"type":"string"})")},
                               {"run_id", utils::Json::parse(R"({"type":"string"})")},
                           },
                           utils::Json::Array{std::string("project_id"), std::string("title")}, true))},
        ToolDescriptor{kMemoryBranchCreate,
                       "Create a memory side-path branch (follow-up / radiating tree).",
                       utils::Json(object_schema(
                           utils::Json::Object{
                               {"project_id", utils::Json::parse(R"({"type":"string"})")},
                               {"title", utils::Json::parse(R"({"type":"string"})")},
                               {"parent_branch_id", utils::Json::parse(R"({"type":"string"})")},
                               {"stage", utils::Json::parse(R"({"type":"string"})")},
                               {"run_id", utils::Json::parse(R"({"type":"string"})")},
                           },
                           utils::Json::Array{std::string("project_id"), std::string("title")}))},
        ToolDescriptor{kMemoryBranchList,
                       "List memory branches (directory of the radiating tree).",
                       utils::Json(object_schema(
                           utils::Json::Object{
                               {"project_id", utils::Json::parse(R"({"type":"string"})")},
                           },
                           utils::Json::Array{std::string("project_id")}))},
    };
}

utils::Json SearchToolService::list_search_modules_json() const {
    const auto items = registry::list_usable_search_modules(registry_, skills_, require_secret_present_,
                                                            has_secret_);
    return registry::usable_modules_to_json(items);
}

registry::UsableSearchModule SearchToolService::require_usable(const std::string& module_id) const {
    const auto items = registry::list_usable_search_modules(registry_, skills_, require_secret_present_,
                                                            has_secret_);
    for (const auto& item : items) {
        if (item.module.id == module_id) {
            return item;
        }
    }
    if (auto mod = registry_.find(module_id)) {
        auto check = registry_.validate(*mod, &skills_, require_secret_present_, has_secret_);
        std::string msg = "search module is not usable: " + module_id;
        for (const auto& e : check.errors) {
            msg.append("; ");
            msg.append(e);
        }
        if (!mod->enabled) {
            msg.append("; module is disabled");
        }
        throw registry::RegistryError(msg);
    }
    throw registry::RegistryError("unknown search module id: " + module_id);
}

utils::Json SearchToolService::get_search_module_skill_json(const std::string& module_id) const {
    return registry::usable_module_to_json(require_usable(module_id));
}

ToolResponse SearchToolService::run_search(const utils::Json& args) {
    ToolResponse resp;
    const std::string module_id = args.at("module_id").as_string("");
    if (module_id == "github") {
        return run_github_search(args);
    }
    if (module_id == "bocha") {
        return run_bocha_search(args);
    }
    resp.error = "run_search supports module_id=github|bocha (got: " + module_id + ")";
    return resp;
}

ToolResponse SearchToolService::run_github_search(const utils::Json& args) {
    ToolResponse resp;
    const std::string module_id = args.at("module_id").as_string("");
    const std::string endpoint = args.at("endpoint").as_string("");
    const std::string q = args.at("q").as_string("");
    require_usable("github");
    if (!require_github_connected(resp)) {
        return resp;
    }
    std::vector<std::pair<std::string, std::string>> extra;
    for (const char* key : {"sort", "order", "search_type", "advanced_search"}) {
        if (args.contains(key)) {
            const auto v = args.at(key).as_string("");
            if (!v.empty()) {
                extra.emplace_back(key, v);
            }
        }
    }
    if (args.contains("page")) {
        extra.emplace_back("page", std::to_string(args.at("page").as_int64(1)));
    }
    if (args.contains("per_page")) {
        extra.emplace_back("per_page", std::to_string(args.at("per_page").as_int64(30)));
    }
    const bool text_match = args.contains("text_match") && args.at("text_match").as_bool(false);
    auto rest = make_rest();
    auto http_resp = rest.search(endpoint, q, extra, text_match);
    utils::Json::Object out;
    out.emplace("module_id", module_id);
    out.emplace("endpoint", endpoint);
    out.emplace("q", q);
    out.emplace("response", providers::github::RestClient::response_to_json(http_resp));
    resp.ok = http_resp.status >= 200 && http_resp.status < 300;
    resp.result = utils::Json(std::move(out));
    if (!resp.ok) {
        std::string err = "GitHub search HTTP " + std::to_string(http_resp.status);
        if (!http_resp.body.empty()) {
            auto snippet = http_resp.body.substr(0, std::min<std::size_t>(http_resp.body.size(), 240));
            err.append(": ");
            err.append(snippet);
        }
        if (http_resp.status == 401 || http_resp.status == 403) {
            err.append(" — check GitHub login/PAT scopes, or wait if rate-limited");
        }
        resp.error = std::move(err);
    }
    return resp;
}

ToolResponse SearchToolService::run_bocha_search(const utils::Json& args) {
    ToolResponse resp;
    const std::string module_id = args.at("module_id").as_string("");
    const std::string endpoint = args.at("endpoint").as_string("");
    require_usable("bocha");
    if (!require_bocha_key(resp)) {
        return resp;
    }

    std::string query = args.contains("q") ? args.at("q").as_string("") : "";
    if (query.empty() && args.contains("query")) {
        query = args.at("query").as_string("");
    }
    if (query.empty()) {
        resp.error = "bocha run_search requires q or query";
        return resp;
    }

    utils::Json::Object body;
    body.emplace("query", query);
    if (args.contains("freshness")) {
        body.emplace("freshness", args.at("freshness").as_string("noLimit"));
    } else {
        body.emplace("freshness", std::string("noLimit"));
    }
    if (args.contains("count")) {
        body.emplace("count", args.at("count").as_int64(10));
    } else {
        body.emplace("count", static_cast<std::int64_t>(10));
    }
    if (endpoint == "web-search") {
        const bool summary = !args.contains("summary") || args.at("summary").as_bool(true);
        body.emplace("summary", summary);
    } else if (endpoint == "ai-search") {
        body.emplace("answer", args.contains("answer") ? args.at("answer").as_bool(false) : false);
        body.emplace("stream", args.contains("stream") ? args.at("stream").as_bool(false) : false);
    } else {
        resp.error = "bocha endpoint must be web-search or ai-search";
        return resp;
    }

    try {
        auto client = make_bocha();
        auto http_resp = client.search(endpoint, utils::Json(std::move(body)));
        utils::Json::Object out;
        out.emplace("module_id", module_id);
        out.emplace("endpoint", endpoint);
        out.emplace("q", query);
        out.emplace("response", providers::bocha::Client::response_to_json(http_resp));
        resp.ok = http_resp.status >= 200 && http_resp.status < 300;
        resp.result = utils::Json(std::move(out));
        if (!resp.ok) {
            resp.error = "Bocha search HTTP " + std::to_string(http_resp.status);
        }
    } catch (const std::exception& ex) {
        resp.error = ex.what();
    }
    return resp;
}

ToolResponse SearchToolService::github_rest(const utils::Json& args) {
    ToolResponse resp;
    if (!require_github_connected(resp)) {
        return resp;
    }
    providers::github::RestCall call;
    call.method = network::HttpMethod::Get;
    if (args.contains("method")) {
        const auto m = args.at("method").as_string("get");
        if (m == "get" || m == "GET") {
            call.method = network::HttpMethod::Get;
        } else if (m == "head" || m == "HEAD") {
            call.method = network::HttpMethod::Head;
        } else if (m == "post" || m == "POST") {
            call.method = network::HttpMethod::Post;
        } else if (m == "put" || m == "PUT") {
            call.method = network::HttpMethod::Put;
        } else if (m == "patch" || m == "PATCH") {
            call.method = network::HttpMethod::Patch;
        } else if (m == "delete" || m == "DELETE") {
            call.method = network::HttpMethod::Delete;
        } else {
            resp.error = "unsupported method: " + m;
            return resp;
        }
    }
    call.path = args.at("path").as_string("");
    call.query = query_object(args);
    if (args.contains("body")) {
        call.body = args.at("body").is_string() ? args.at("body").as_string("") : args.at("body").dump(0);
    }
    if (args.contains("accept")) {
        call.accept = args.at("accept").as_string("");
    }
    call.text_match = args.contains("text_match") && args.at("text_match").as_bool(false);
    auto rest = make_rest();
    auto http_resp = rest.call(call);
    resp.ok = http_resp.status >= 200 && http_resp.status < 300;
    resp.result = providers::github::RestClient::response_to_json(http_resp, enrich_opts(args));
    if (!resp.ok) {
        std::string err = "GitHub REST HTTP " + std::to_string(http_resp.status);
        if (!http_resp.body.empty()) {
            err.append(": ");
            err.append(http_resp.body.substr(0, std::min<std::size_t>(http_resp.body.size(), 240)));
        }
        if (http_resp.status == 401 || http_resp.status == 403) {
            err.append(" — check GitHub login/PAT scopes, or wait if rate-limited");
        }
        resp.error = std::move(err);
    }
    return resp;
}

ToolResponse SearchToolService::github_rest_paginate(const utils::Json& args) {
    ToolResponse resp;
    if (!require_github_connected(resp)) {
        return resp;
    }
    providers::github::PageCollectOptions page_opts;
    if (args.contains("per_page")) {
        page_opts.per_page = static_cast<int>(args.at("per_page").as_int64(100));
    }
    if (args.contains("max_pages")) {
        page_opts.max_pages = static_cast<int>(args.at("max_pages").as_int64(100));
    }
    const std::string path = args.at("path").as_string("");
    const std::string accept = args.contains("accept") ? args.at("accept").as_string("") : "";
    auto rest = make_rest();
    auto pages = rest.collect_pages(path, query_object(args), page_opts, accept);
    resp.result = providers::github::RestClient::pages_to_json(pages, enrich_opts(args));
    resp.ok = !pages.empty() && pages.front().status >= 200 && pages.front().status < 300;
    if (!resp.ok && !pages.empty()) {
        resp.error = "GitHub REST paginate HTTP " + std::to_string(pages.front().status);
    }
    return resp;
}

ToolResponse SearchToolService::github_resource(const utils::Json& args) {
    ToolResponse resp;
    if (!require_github_connected(resp)) {
        return resp;
    }
    const std::string resource = args.at("resource").as_string("");
    const bool paginate = args.contains("paginate") && args.at("paginate").as_bool(false);
    auto rest = make_rest();
    const auto enrich = enrich_opts(args);

    // List-like resources can auto-paginate when requested.
    static const char* kPageable[] = {"commits", "issues",        "issue_comments", "pulls",
                                      "pull_files", "pull_commits", "releases",       "branches",
                                      "tags",      "contributors",  "forks"};
    bool is_pageable = false;
    for (const char* n : kPageable) {
        if (resource == n) {
            is_pageable = true;
            break;
        }
    }

    if (paginate && is_pageable) {
        // Resolve path via a first lightweight construction: call resource once is wasteful;
        // map resource name to path using the same helpers by issuing collect on known paths.
        std::string owner = args.contains("owner") ? args.at("owner").as_string("") : "";
        std::string repo = args.contains("repo") ? args.at("repo").as_string("") : "";
        const int number = args.contains("number") ? static_cast<int>(args.at("number").as_int64(0)) : 0;
        std::string path;
        if (resource == "commits") {
            path = providers::github::RestClient::repo_path(owner, repo, "/commits");
        } else if (resource == "issues") {
            path = providers::github::RestClient::repo_path(owner, repo, "/issues");
        } else if (resource == "issue_comments") {
            path = providers::github::RestClient::repo_path(
                owner, repo, "/issues/" + std::to_string(number) + "/comments");
        } else if (resource == "pulls") {
            path = providers::github::RestClient::repo_path(owner, repo, "/pulls");
        } else if (resource == "pull_files") {
            path = providers::github::RestClient::repo_path(
                owner, repo, "/pulls/" + std::to_string(number) + "/files");
        } else if (resource == "pull_commits") {
            path = providers::github::RestClient::repo_path(
                owner, repo, "/pulls/" + std::to_string(number) + "/commits");
        } else if (resource == "releases") {
            path = providers::github::RestClient::repo_path(owner, repo, "/releases");
        } else if (resource == "branches") {
            path = providers::github::RestClient::repo_path(owner, repo, "/branches");
        } else if (resource == "tags") {
            path = providers::github::RestClient::repo_path(owner, repo, "/tags");
        } else if (resource == "contributors") {
            path = providers::github::RestClient::repo_path(owner, repo, "/contributors");
        } else if (resource == "forks") {
            path = providers::github::RestClient::repo_path(owner, repo, "/forks");
        }
        providers::github::PageCollectOptions page_opts;
        if (args.contains("per_page")) {
            page_opts.per_page = static_cast<int>(args.at("per_page").as_int64(100));
        }
        if (args.contains("max_pages")) {
            page_opts.max_pages = static_cast<int>(args.at("max_pages").as_int64(100));
        }
        auto pages = rest.collect_pages(path, query_object(args), page_opts, {});
        utils::Json::Object out;
        out.emplace("resource", resource);
        out.emplace("paginated", true);
        out.emplace("result", providers::github::RestClient::pages_to_json(pages, enrich));
        resp.result = utils::Json(std::move(out));
        resp.ok = !pages.empty() && pages.front().status >= 200 && pages.front().status < 300;
        if (!resp.ok && !pages.empty()) {
            resp.error = "GitHub resource HTTP " + std::to_string(pages.front().status);
        }
        return resp;
    }

    auto http_resp = rest.resource(resource, args);
    // Default decode for contents/readme/git_blob when decode_content omitted.
    auto opts = enrich;
    if (!args.contains("decode_content") &&
        (resource == "contents" || resource == "readme" || resource == "git_blob")) {
        opts.decode_base64_content = true;
    }
    utils::Json::Object out;
    out.emplace("resource", resource);
    out.emplace("paginated", false);
    out.emplace("response", providers::github::RestClient::response_to_json(http_resp, opts));
    resp.ok = http_resp.status >= 200 && http_resp.status < 300;
    resp.result = utils::Json(std::move(out));
    if (!resp.ok) {
        resp.error = "GitHub resource HTTP " + std::to_string(http_resp.status);
    }
    return resp;
}

namespace {

std::string kg_make_id(const char* prefix) {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<std::uint64_t> dist;
    std::ostringstream oss;
    oss << prefix << std::hex << dist(rng);
    return oss.str();
}

} // namespace

ToolResponse SearchToolService::knowledge_graph_get(const utils::Json& args) {
    ToolResponse resp;
    if (!open_project_db_) {
        resp.error = "knowledge graph unavailable (no project db opener)";
        return resp;
    }
    const auto project_id = args.at("project_id").as_string("");
    auto db = open_project_db_(project_id);
    research::KnowledgeGraphStore kg;
    kg.open(db);
    resp.ok = true;
    resp.result = kg.graph_json(project_id);
    kg.close();
    db.close();
    return resp;
}

ToolResponse SearchToolService::knowledge_graph_add(const utils::Json& args) {
    ToolResponse resp;
    if (!open_project_db_) {
        resp.error = "knowledge graph unavailable (no project db opener)";
        return resp;
    }
    const bool valid = !args.contains("valid") || args.at("valid").as_bool(true);
    if (!valid) {
        resp.ok = true;
        resp.result = utils::Json(utils::Json::Object{
            {"skipped", true},
            {"reason", std::string("invalid knowledge rejected — not uploaded")},
        });
        return resp;
    }
    const auto project_id = args.at("project_id").as_string("");
    research::KnowledgeNode node;
    node.id = args.contains("id") ? args.at("id").as_string("") : kg_make_id("kn_");
    if (node.id.empty()) {
        node.id = kg_make_id("kn_");
    }
    node.project_id = project_id;
    node.run_id = args.contains("run_id") ? args.at("run_id").as_string("") : "";
    node.title = args.at("title").as_string("");
    node.content = args.contains("content") ? args.at("content").as_string("") : "";
    node.kind = args.contains("kind") ? args.at("kind").as_string("fact") : "fact";
    node.direction_id = args.contains("direction_id") ? args.at("direction_id").as_string("") : "";
    node.depth_layer =
        args.contains("depth_layer") ? static_cast<int>(args.at("depth_layer").as_int64(0)) : 0;
    node.valid = true;
    node.meta_json = args.contains("meta_json") ? args.at("meta_json").as_string("") : "";

    auto db = open_project_db_(project_id);
    research::KnowledgeGraphStore kg;
    kg.open(db);
    kg.upsert_node(node);
    resp.ok = true;
    resp.result = utils::Json(utils::Json::Object{
        {"id", node.id},
        {"uploaded", true},
    });
    kg.close();
    db.close();
    return resp;
}

ToolResponse SearchToolService::knowledge_graph_update(const utils::Json& args) {
    ToolResponse resp;
    if (!open_project_db_) {
        resp.error = "knowledge graph unavailable (no project db opener)";
        return resp;
    }
    const auto project_id = args.at("project_id").as_string("");
    research::KnowledgeNode node;
    node.id = args.at("id").as_string("");
    node.project_id = project_id;
    node.title = args.contains("title") ? args.at("title").as_string("") : "";
    node.content = args.contains("content") ? args.at("content").as_string("") : "";
    node.kind = args.contains("kind") ? args.at("kind").as_string("") : "";
    node.valid = !args.contains("valid") || args.at("valid").as_bool(true);
    node.direction_id = args.contains("direction_id") ? args.at("direction_id").as_string("") : "";
    node.depth_layer =
        args.contains("depth_layer") ? static_cast<int>(args.at("depth_layer").as_int64(0)) : 0;
    node.meta_json = args.contains("meta_json") ? args.at("meta_json").as_string("") : "";
    node.run_id = args.contains("run_id") ? args.at("run_id").as_string("") : "";

    auto db = open_project_db_(project_id);
    research::KnowledgeGraphStore kg;
    kg.open(db);
    const bool ok = kg.update_node(node);
    resp.ok = ok;
    if (!ok) {
        resp.error = "node not found";
    } else {
        resp.result = utils::Json(utils::Json::Object{{"id", node.id}, {"updated", true}});
    }
    kg.close();
    db.close();
    return resp;
}

ToolResponse SearchToolService::knowledge_graph_delete(const utils::Json& args) {
    ToolResponse resp;
    if (!open_project_db_) {
        resp.error = "knowledge graph unavailable (no project db opener)";
        return resp;
    }
    const auto project_id = args.at("project_id").as_string("");
    auto db = open_project_db_(project_id);
    research::KnowledgeGraphStore kg;
    kg.open(db);
    if (args.contains("edge_id") && !args.at("edge_id").as_string("").empty()) {
        kg.delete_edge(project_id, args.at("edge_id").as_string(""));
        resp.ok = true;
        resp.result = utils::Json(utils::Json::Object{{"deleted_edge", args.at("edge_id").as_string("")}});
    } else if (args.contains("node_id") && !args.at("node_id").as_string("").empty()) {
        kg.delete_node(project_id, args.at("node_id").as_string(""));
        resp.ok = true;
        resp.result = utils::Json(utils::Json::Object{{"deleted_node", args.at("node_id").as_string("")}});
    } else if (args.contains("id") && !args.at("id").as_string("").empty()) {
        kg.delete_node(project_id, args.at("id").as_string(""));
        resp.ok = true;
        resp.result = utils::Json(utils::Json::Object{{"deleted_node", args.at("id").as_string("")}});
    } else {
        resp.error = "node_id or edge_id required";
    }
    kg.close();
    db.close();
    return resp;
}

ToolResponse SearchToolService::knowledge_graph_link(const utils::Json& args) {
    ToolResponse resp;
    if (!open_project_db_) {
        resp.error = "knowledge graph unavailable (no project db opener)";
        return resp;
    }
    const auto project_id = args.at("project_id").as_string("");
    research::KnowledgeEdge edge;
    edge.id = args.contains("id") ? args.at("id").as_string("") : kg_make_id("ke_");
    if (edge.id.empty()) {
        edge.id = kg_make_id("ke_");
    }
    edge.project_id = project_id;
    edge.from_id = args.at("from_id").as_string("");
    edge.to_id = args.at("to_id").as_string("");
    edge.relation = args.contains("relation") ? args.at("relation").as_string("related") : "related";
    edge.meta_json = args.contains("meta_json") ? args.at("meta_json").as_string("") : "";

    auto db = open_project_db_(project_id);
    research::KnowledgeGraphStore kg;
    kg.open(db);
    kg.upsert_edge(edge);
    resp.ok = true;
    resp.result = utils::Json(utils::Json::Object{{"id", edge.id}, {"linked", true}});
    kg.close();
    db.close();
    return resp;
}

ToolResponse SearchToolService::knowledge_graph_catalog(const utils::Json& args) {
    ToolResponse resp;
    if (!open_project_db_) {
        resp.error = "knowledge graph unavailable (no project db opener)";
        return resp;
    }
    const auto project_id = args.at("project_id").as_string("");
    auto db = open_project_db_(project_id);
    research::KnowledgeGraphStore kg;
    kg.open(db);
    resp.ok = true;
    resp.result = kg.catalog_json(project_id);
    kg.close();
    db.close();
    return resp;
}

ToolResponse SearchToolService::memory_catalog(const utils::Json& args) {
    ToolResponse resp;
    if (!open_project_db_) {
        resp.error = "memory tree unavailable (no project db opener)";
        return resp;
    }
    const auto project_id = args.at("project_id").as_string("");
    auto db = open_project_db_(project_id);
    research::MemoryTreeStore mem;
    mem.open(db);
    resp.ok = true;
    resp.result = mem.catalog_json(project_id);
    mem.close();
    db.close();
    return resp;
}

ToolResponse SearchToolService::memory_get(const utils::Json& args) {
    ToolResponse resp;
    if (!open_project_db_) {
        resp.error = "memory tree unavailable (no project db opener)";
        return resp;
    }
    const auto project_id = args.at("project_id").as_string("");
    const auto id = args.at("id").as_string("");
    auto db = open_project_db_(project_id);
    research::MemoryTreeStore mem;
    mem.open(db);
    auto entry = mem.get_entry(project_id, id);
    if (!entry) {
        resp.error = "memory entry not found";
    } else {
        resp.ok = true;
        resp.result = utils::Json(utils::Json::Object{
            {"id", entry->id},
            {"branch_id", entry->branch_id},
            {"parent_id", entry->parent_id},
            {"title", entry->title},
            {"summary", entry->summary},
            {"body", entry->body},
            {"kind", entry->kind},
            {"direction_id", entry->direction_id},
            {"depth_layer", static_cast<std::int64_t>(entry->depth_layer)},
        });
    }
    mem.close();
    db.close();
    return resp;
}

ToolResponse SearchToolService::memory_chain(const utils::Json& args) {
    ToolResponse resp;
    if (!open_project_db_) {
        resp.error = "memory tree unavailable (no project db opener)";
        return resp;
    }
    const auto project_id = args.at("project_id").as_string("");
    const auto id = args.at("id").as_string("");
    auto db = open_project_db_(project_id);
    research::MemoryTreeStore mem;
    mem.open(db);
    resp.ok = true;
    resp.result = mem.chain_json(project_id, id);
    mem.close();
    db.close();
    return resp;
}

ToolResponse SearchToolService::memory_add(const utils::Json& args) {
    ToolResponse resp;
    if (!open_project_db_) {
        resp.error = "memory tree unavailable (no project db opener)";
        return resp;
    }
    const auto project_id = args.at("project_id").as_string("");
    research::MemoryEntry entry;
    entry.id = args.contains("id") ? args.at("id").as_string("") : kg_make_id("mem_");
    if (entry.id.empty()) {
        entry.id = kg_make_id("mem_");
    }
    entry.project_id = project_id;
    entry.branch_id = args.contains("branch_id") ? args.at("branch_id").as_string("") : "";
    entry.parent_id = args.contains("parent_id") ? args.at("parent_id").as_string("") : "";
    entry.run_id = args.contains("run_id") ? args.at("run_id").as_string("") : "";
    entry.title = args.at("title").as_string("");
    entry.summary = args.contains("summary") ? args.at("summary").as_string("") : "";
    entry.body = args.contains("body") ? args.at("body").as_string("") : "";
    entry.kind = args.contains("kind") ? args.at("kind").as_string("note") : "note";
    entry.direction_id = args.contains("direction_id") ? args.at("direction_id").as_string("") : "";
    entry.depth_layer =
        args.contains("depth_layer") ? static_cast<int>(args.at("depth_layer").as_int64(0)) : 0;

    auto db = open_project_db_(project_id);
    research::MemoryTreeStore mem;
    mem.open(db);
    if (entry.branch_id.empty()) {
        auto branches = mem.list_branches(project_id);
        if (!branches.empty()) {
            entry.branch_id = branches.front().id;
        } else {
            research::MemoryBranch b;
            b.id = kg_make_id("mb_");
            b.project_id = project_id;
            b.title = "main";
            b.stage = "research";
            b.run_id = entry.run_id;
            mem.upsert_branch(b);
            entry.branch_id = b.id;
        }
    }
    if (entry.summary.empty() && !entry.body.empty()) {
        entry.summary = entry.body.substr(0, std::min<std::size_t>(entry.body.size(), 160));
    }
    mem.upsert_entry(entry);
    resp.ok = true;
    resp.result = utils::Json(utils::Json::Object{
        {"id", entry.id},
        {"branch_id", entry.branch_id},
        {"stored", true},
    });
    mem.close();
    db.close();
    return resp;
}

ToolResponse SearchToolService::memory_branch_create(const utils::Json& args) {
    ToolResponse resp;
    if (!open_project_db_) {
        resp.error = "memory tree unavailable (no project db opener)";
        return resp;
    }
    const auto project_id = args.at("project_id").as_string("");
    research::MemoryBranch b;
    b.id = args.contains("id") ? args.at("id").as_string("") : kg_make_id("mb_");
    if (b.id.empty()) {
        b.id = kg_make_id("mb_");
    }
    b.project_id = project_id;
    b.parent_branch_id =
        args.contains("parent_branch_id") ? args.at("parent_branch_id").as_string("") : "";
    b.title = args.at("title").as_string("");
    b.stage = args.contains("stage") ? args.at("stage").as_string("research") : "research";
    b.run_id = args.contains("run_id") ? args.at("run_id").as_string("") : "";

    auto db = open_project_db_(project_id);
    research::MemoryTreeStore mem;
    mem.open(db);
    mem.upsert_branch(b);
    resp.ok = true;
    resp.result = utils::Json(utils::Json::Object{{"id", b.id}, {"created", true}});
    mem.close();
    db.close();
    return resp;
}

ToolResponse SearchToolService::memory_branch_list(const utils::Json& args) {
    ToolResponse resp;
    if (!open_project_db_) {
        resp.error = "memory tree unavailable (no project db opener)";
        return resp;
    }
    const auto project_id = args.at("project_id").as_string("");
    auto db = open_project_db_(project_id);
    research::MemoryTreeStore mem;
    mem.open(db);
    utils::Json::Array arr;
    for (const auto& b : mem.list_branches(project_id)) {
        arr.push_back(utils::Json(utils::Json::Object{
            {"id", b.id},
            {"parent_branch_id", b.parent_branch_id},
            {"title", b.title},
            {"stage", b.stage},
        }));
    }
    resp.ok = true;
    resp.result = utils::Json(utils::Json::Object{
        {"project_id", project_id},
        {"branches", std::move(arr)},
    });
    mem.close();
    db.close();
    return resp;
}

ToolResponse SearchToolService::call(const ToolRequest& request) {
    ToolResponse resp;
    try {
        if (request.name == kListSearchModules) {
            resp.ok = true;
            resp.result = list_search_modules_json();
            return resp;
        }
        if (request.name == kGetSearchModuleSkill) {
            if (!request.arguments.is_object() || !request.arguments.contains("module_id")) {
                resp.error = "module_id is required";
                return resp;
            }
            resp.ok = true;
            resp.result = get_search_module_skill_json(request.arguments.at("module_id").as_string(""));
            return resp;
        }
        if (request.name == kGithubOAuthStart) {
            std::string scope;
            bool open_browser = true;
            if (request.arguments.is_object()) {
                if (request.arguments.contains("scope")) {
                    scope = request.arguments.at("scope").as_string("");
                }
                if (request.arguments.contains("open_browser")) {
                    open_browser = request.arguments.at("open_browser").as_bool(true);
                }
            }
            const auto auth = github_oauth_.start(scope, open_browser);
            utils::Json::Object obj;
            obj.emplace("device_code", auth.device_code);
            obj.emplace("user_code", auth.user_code);
            obj.emplace("verification_uri", auth.verification_uri);
            obj.emplace("verification_uri_complete", auth.verification_uri_complete);
            obj.emplace("expires_in", auth.expires_in);
            obj.emplace("interval", auth.interval);
            obj.emplace("browser_opened", open_browser);
            resp.ok = true;
            resp.result = utils::Json(std::move(obj));
            return resp;
        }
        if (request.name == kGithubOAuthPoll) {
            if (!request.arguments.is_object() || !request.arguments.contains("device_code")) {
                resp.error = "device_code is required";
                return resp;
            }
            const auto poll = github_oauth_.poll(request.arguments.at("device_code").as_string(""));
            utils::Json::Object obj;
            obj.emplace("status", poll_status_name(poll.status));
            obj.emplace("error", poll.error);
            obj.emplace("error_description", poll.error_description);
            obj.emplace("interval", poll.interval);
            if (poll.status == auth::PollStatus::Authorized) {
                obj.emplace("connection", github_oauth_.status_json());
            }
            resp.ok = poll.status == auth::PollStatus::Authorized ||
                      poll.status == auth::PollStatus::Pending ||
                      poll.status == auth::PollStatus::SlowDown;
            resp.result = utils::Json(std::move(obj));
            if (poll.status == auth::PollStatus::Expired || poll.status == auth::PollStatus::AccessDenied ||
                poll.status == auth::PollStatus::Error) {
                resp.error = poll.error.empty() ? poll_status_name(poll.status) : poll.error;
                resp.ok = false;
            }
            return resp;
        }
        if (request.name == kGithubOAuthStatus) {
            resp.ok = true;
            resp.result = github_oauth_.status_json();
            return resp;
        }
        if (request.name == kGithubOAuthDisconnect) {
            github_oauth_.disconnect();
            resp.ok = true;
            resp.result = github_oauth_.status_json();
            return resp;
        }
        if (request.name == kGithubOAuthSetPat) {
            if (!request.arguments.is_object() || !request.arguments.contains("token")) {
                resp.error = "token is required";
                return resp;
            }
            const std::string scope =
                request.arguments.contains("scope") ? request.arguments.at("scope").as_string("") : "";
            github_oauth_.set_pat(request.arguments.at("token").as_string(""), scope);
            resp.ok = true;
            resp.result = github_oauth_.status_json();
            return resp;
        }
        if (request.name == kRunSearch) {
            if (!request.arguments.is_object()) {
                resp.error = "arguments object required";
                return resp;
            }
            return run_search(request.arguments);
        }
        if (request.name == kGithubRestGet || request.name == kGithubRest) {
            if (!request.arguments.is_object() || !request.arguments.contains("path")) {
                resp.error = "path is required";
                return resp;
            }
            utils::Json args = request.arguments;
            if (request.name == kGithubRestGet && !args.contains("method")) {
                args["method"] = std::string("get");
            }
            return github_rest(args);
        }
        if (request.name == kGithubRestPaginate) {
            if (!request.arguments.is_object() || !request.arguments.contains("path")) {
                resp.error = "path is required";
                return resp;
            }
            return github_rest_paginate(request.arguments);
        }
        if (request.name == kGithubResource) {
            if (!request.arguments.is_object() || !request.arguments.contains("resource")) {
                resp.error = "resource is required";
                return resp;
            }
            return github_resource(request.arguments);
        }
        if (request.name == kGithubRestCatalog) {
            resp.ok = true;
            resp.result = providers::github::RestClient::resource_catalog_json();
            return resp;
        }
        if (request.name == kKnowledgeGraphGet) {
            if (!request.arguments.is_object() || !request.arguments.contains("project_id")) {
                resp.error = "project_id is required";
                return resp;
            }
            return knowledge_graph_get(request.arguments);
        }
        if (request.name == kKnowledgeGraphAdd) {
            if (!request.arguments.is_object() || !request.arguments.contains("project_id")) {
                resp.error = "project_id is required";
                return resp;
            }
            return knowledge_graph_add(request.arguments);
        }
        if (request.name == kKnowledgeGraphUpdate) {
            if (!request.arguments.is_object() || !request.arguments.contains("project_id") ||
                !request.arguments.contains("id")) {
                resp.error = "project_id and id are required";
                return resp;
            }
            return knowledge_graph_update(request.arguments);
        }
        if (request.name == kKnowledgeGraphDelete) {
            if (!request.arguments.is_object() || !request.arguments.contains("project_id")) {
                resp.error = "project_id is required";
                return resp;
            }
            return knowledge_graph_delete(request.arguments);
        }
        if (request.name == kKnowledgeGraphLink) {
            if (!request.arguments.is_object() || !request.arguments.contains("project_id") ||
                !request.arguments.contains("from_id") || !request.arguments.contains("to_id")) {
                resp.error = "project_id, from_id, and to_id are required";
                return resp;
            }
            return knowledge_graph_link(request.arguments);
        }
        if (request.name == kKnowledgeGraphCatalog) {
            if (!request.arguments.is_object() || !request.arguments.contains("project_id")) {
                resp.error = "project_id is required";
                return resp;
            }
            return knowledge_graph_catalog(request.arguments);
        }
        if (request.name == kMemoryCatalog) {
            if (!request.arguments.is_object() || !request.arguments.contains("project_id")) {
                resp.error = "project_id is required";
                return resp;
            }
            return memory_catalog(request.arguments);
        }
        if (request.name == kMemoryGet) {
            if (!request.arguments.is_object() || !request.arguments.contains("project_id") ||
                !request.arguments.contains("id")) {
                resp.error = "project_id and id are required";
                return resp;
            }
            return memory_get(request.arguments);
        }
        if (request.name == kMemoryChain) {
            if (!request.arguments.is_object() || !request.arguments.contains("project_id") ||
                !request.arguments.contains("id")) {
                resp.error = "project_id and id are required";
                return resp;
            }
            return memory_chain(request.arguments);
        }
        if (request.name == kMemoryAdd) {
            if (!request.arguments.is_object() || !request.arguments.contains("project_id") ||
                !request.arguments.contains("title")) {
                resp.error = "project_id and title are required";
                return resp;
            }
            return memory_add(request.arguments);
        }
        if (request.name == kMemoryBranchCreate) {
            if (!request.arguments.is_object() || !request.arguments.contains("project_id") ||
                !request.arguments.contains("title")) {
                resp.error = "project_id and title are required";
                return resp;
            }
            return memory_branch_create(request.arguments);
        }
        if (request.name == kMemoryBranchList) {
            if (!request.arguments.is_object() || !request.arguments.contains("project_id")) {
                resp.error = "project_id is required";
                return resp;
            }
            return memory_branch_list(request.arguments);
        }
        resp.error = "unknown tool: " + request.name;
        return resp;
    } catch (const std::exception& ex) {
        resp.ok = false;
        resp.error = ex.what();
        return resp;
    }
}

} // namespace xscope::mcp
