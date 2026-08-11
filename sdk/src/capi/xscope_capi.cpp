#include "xscope/capi/xscope_capi.h"

#include "xscope/research/evidence_store.hpp"
#include "xscope/research/knowledge_graph.hpp"
#include "xscope/research/memory_tree.hpp"
#include "xscope/research/orchestrator.hpp"
#include "xscope/storage/workspace.hpp"
#include "xscope/utils/json.hpp"
#include "xscope/utils/path.hpp"
#include "xscope/xaiop/bridge.hpp"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>

namespace {

thread_local std::string g_last_error;

void set_error(const std::string& msg) { g_last_error = msg; }

char* dup_cstr(const std::string& s) {
    char* p = static_cast<char*>(std::malloc(s.size() + 1));
    if (!p) {
        return nullptr;
    }
    std::memcpy(p, s.data(), s.size());
    p[s.size()] = '\0';
    return p;
}

char* json_ok(xscope::utils::Json::Object obj) {
    obj.emplace("ok", true);
    return dup_cstr(xscope::utils::Json(std::move(obj)).dump(0));
}

char* json_err(const std::string& error) {
    set_error(error);
    xscope::utils::Json::Object obj;
    obj.emplace("ok", false);
    obj.emplace("error", error);
    return dup_cstr(xscope::utils::Json(std::move(obj)).dump(0));
}

std::string poll_status_name(xscope::auth::PollStatus s) {
    switch (s) {
    case xscope::auth::PollStatus::Pending:
        return "authorization_pending";
    case xscope::auth::PollStatus::SlowDown:
        return "slow_down";
    case xscope::auth::PollStatus::Authorized:
        return "authorized";
    case xscope::auth::PollStatus::Expired:
        return "expired_token";
    case xscope::auth::PollStatus::AccessDenied:
        return "access_denied";
    case xscope::auth::PollStatus::Error:
        return "error";
    }
    return "error";
}

} // namespace

struct xscope_workspace {
    xscope::storage::Workspace ws;
    std::unique_ptr<xscope::research::ResearchOrchestrator> research;
};

extern "C" {

void xscope_string_free(char* s) { std::free(s); }

int xscope_last_error(char* buf, int buf_len) {
    const int need = static_cast<int>(g_last_error.size());
    if (buf && buf_len > 0) {
        const int n = need < buf_len - 1 ? need : buf_len - 1;
        if (n > 0) {
            std::memcpy(buf, g_last_error.data(), static_cast<size_t>(n));
        }
        buf[n] = '\0';
    }
    return need;
}

xscope_workspace* xscope_workspace_open(const char* data_root_utf8) {
    if (!data_root_utf8 || !*data_root_utf8) {
        set_error("data_root is empty");
        return nullptr;
    }
    try {
        auto* out = new xscope_workspace();
        out->ws.open(data_root_utf8);
        out->research = std::make_unique<xscope::research::ResearchOrchestrator>(out->ws);
        g_last_error.clear();
        return out;
    } catch (const std::exception& ex) {
        set_error(ex.what());
        return nullptr;
    }
}

void xscope_workspace_close(xscope_workspace* ws) {
    if (!ws) {
        return;
    }
    try {
        // Destroy orchestrator first (joins worker threads) before closing workspace DBs.
        ws->research.reset();
        ws->ws.close();
    } catch (...) {
    }
    delete ws;
}

char* xscope_github_oauth_status(xscope_workspace* ws) {
    if (!ws) {
        return json_err("workspace is null");
    }
    try {
        auto status = ws->ws.github_oauth().status_json();
        xscope::utils::Json::Object obj = status.as_object();
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_github_oauth_start(xscope_workspace* ws, const char* scope_utf8, int open_browser) {
    if (!ws) {
        return json_err("workspace is null");
    }
    try {
        const std::string scope = scope_utf8 ? scope_utf8 : "";
        auto oauth = ws->ws.github_oauth();
        if (oauth.config().client_id.empty()) {
            return json_err(
                "GitHub OAuth client_id missing. Set XSCOPE_GITHUB_OAUTH_CLIENT_ID or "
                "data_root/global/github_oauth.json");
        }
        const auto auth = oauth.start(scope, open_browser != 0);
        xscope::utils::Json::Object obj;
        obj.emplace("device_code", auth.device_code);
        obj.emplace("user_code", auth.user_code);
        obj.emplace("verification_uri", auth.verification_uri);
        obj.emplace("verification_uri_complete", auth.verification_uri_complete);
        obj.emplace("expires_in", auth.expires_in);
        obj.emplace("interval", auth.interval);
        obj.emplace("browser_opened", open_browser != 0);
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_github_oauth_poll(xscope_workspace* ws, const char* device_code_utf8) {
    if (!ws) {
        return json_err("workspace is null");
    }
    if (!device_code_utf8 || !*device_code_utf8) {
        return json_err("device_code is empty");
    }
    try {
        auto oauth = ws->ws.github_oauth();
        const auto poll = oauth.poll(device_code_utf8);
        xscope::utils::Json::Object obj;
        obj.emplace("status", poll_status_name(poll.status));
        obj.emplace("error", poll.error);
        obj.emplace("error_description", poll.error_description);
        obj.emplace("interval", poll.interval);
        if (poll.status == xscope::auth::PollStatus::Authorized) {
            obj.emplace("connection", oauth.status_json());
        }
        const bool soft_ok = poll.status == xscope::auth::PollStatus::Authorized ||
                             poll.status == xscope::auth::PollStatus::Pending ||
                             poll.status == xscope::auth::PollStatus::SlowDown;
        obj.emplace("ok", soft_ok);
        if (!soft_ok) {
            set_error(poll.error.empty() ? poll_status_name(poll.status) : poll.error);
        }
        return dup_cstr(xscope::utils::Json(std::move(obj)).dump(0));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_github_oauth_set_pat(xscope_workspace* ws, const char* token_utf8,
                                  const char* scope_utf8) {
    if (!ws) {
        return json_err("workspace is null");
    }
    if (!token_utf8 || !*token_utf8) {
        return json_err("token is empty");
    }
    try {
        auto oauth = ws->ws.github_oauth();
        oauth.set_pat(token_utf8, scope_utf8 ? scope_utf8 : "");
        auto status = oauth.status_json();
        xscope::utils::Json::Object obj = status.as_object();
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_github_oauth_disconnect(xscope_workspace* ws) {
    if (!ws) {
        return json_err("workspace is null");
    }
    try {
        auto oauth = ws->ws.github_oauth();
        oauth.disconnect();
        auto status = oauth.status_json();
        xscope::utils::Json::Object obj = status.as_object();
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_secret_put(xscope_workspace* ws, const char* id_utf8, const char* provider_utf8,
                        const char* plaintext_utf8) {
    if (!ws) {
        return json_err("workspace is null");
    }
    if (!id_utf8 || !*id_utf8) {
        return json_err("id is empty");
    }
    if (!plaintext_utf8 || !*plaintext_utf8) {
        return json_err("plaintext is empty");
    }
    try {
        ws->ws.put_secret(id_utf8, provider_utf8 ? provider_utf8 : "", plaintext_utf8);
        xscope::utils::Json::Object obj;
        obj.emplace("id", std::string(id_utf8));
        obj.emplace("present", true);
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_secret_has(xscope_workspace* ws, const char* id_utf8) {
    if (!ws) {
        return json_err("workspace is null");
    }
    if (!id_utf8 || !*id_utf8) {
        return json_err("id is empty");
    }
    try {
        auto secret = ws->ws.get_secret(id_utf8);
        xscope::utils::Json::Object obj;
        obj.emplace("id", std::string(id_utf8));
        obj.emplace("present", secret.has_value() && !secret->empty());
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_secret_remove(xscope_workspace* ws, const char* id_utf8) {
    if (!ws) {
        return json_err("workspace is null");
    }
    if (!id_utf8 || !*id_utf8) {
        return json_err("id is empty");
    }
    try {
        ws->ws.remove_secret(id_utf8);
        xscope::utils::Json::Object obj;
        obj.emplace("id", std::string(id_utf8));
        obj.emplace("present", false);
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_ai_provider_status(xscope_workspace* ws) {
    if (!ws) {
        return json_err("workspace is null");
    }
    try {
        ws->ws.ensure_ai_providers();
        auto status = ws->ws.ai_runtime().providers_status_json();
        xscope::utils::Json::Object obj = status.as_object();
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_ai_set_api_key(xscope_workspace* ws, const char* provider_id_utf8,
                            const char* api_key_utf8) {
    if (!ws) {
        return json_err("workspace is null");
    }
    if (!provider_id_utf8 || !*provider_id_utf8) {
        return json_err("provider_id is empty");
    }
    if (!api_key_utf8 || !*api_key_utf8) {
        return json_err("api_key is empty");
    }
    try {
        ws->ws.ensure_ai_providers();
        auto provider = ws->ws.ai_registry().find_provider(provider_id_utf8);
        if (!provider) {
            return json_err(std::string("unknown provider id: ") + provider_id_utf8);
        }
        ws->ws.put_secret(provider->auth.secret_id, provider->id, api_key_utf8);
        auto refreshed = ws->ws.ai_runtime().refresh_models_from_api(provider_id_utf8);
        xscope::utils::Json::Object obj = refreshed.as_object();
        obj.emplace("secret_id", provider->auth.secret_id);
        obj.emplace("secret_present", true);
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_ai_refresh_models(xscope_workspace* ws, const char* provider_id_utf8) {
    if (!ws) {
        return json_err("workspace is null");
    }
    if (!provider_id_utf8 || !*provider_id_utf8) {
        return json_err("provider_id is empty");
    }
    try {
        ws->ws.ensure_ai_providers();
        auto refreshed = ws->ws.ai_runtime().refresh_models_from_api(provider_id_utf8);
        xscope::utils::Json::Object obj = refreshed.as_object();
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_ai_set_preferred_model(xscope_workspace* ws, const char* provider_id_utf8,
                                    const char* model_id_utf8) {
    if (!ws) {
        return json_err("workspace is null");
    }
    if (!provider_id_utf8 || !*provider_id_utf8) {
        return json_err("provider_id is empty");
    }
    if (!model_id_utf8 || !*model_id_utf8) {
        return json_err("model_id is empty");
    }
    try {
        ws->ws.ensure_ai_providers();
        ws->ws.ai_registry().set_preferred_model(provider_id_utf8, model_id_utf8);
        ws->ws.ai_registry().save();
        xscope::utils::Json::Object obj;
        obj.emplace("provider_id", std::string(provider_id_utf8));
        obj.emplace("preferred_model_id", std::string(model_id_utf8));
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_ai_set_model_capabilities(xscope_workspace* ws, const char* provider_id_utf8,
                                       const char* capabilities_json_utf8) {
    if (!ws) {
        return json_err("workspace is null");
    }
    if (!provider_id_utf8 || !*provider_id_utf8) {
        return json_err("provider_id is empty");
    }
    if (!capabilities_json_utf8 || !*capabilities_json_utf8) {
        return json_err("capabilities_json is empty");
    }
    try {
        ws->ws.ensure_ai_providers();
        const auto parsed = xscope::utils::Json::parse(capabilities_json_utf8);
        if (!parsed.is_array()) {
            return json_err("capabilities_json must be a JSON array");
        }
        std::vector<std::string> caps;
        for (const auto& c : parsed.as_array()) {
            if (c.is_string()) {
                caps.push_back(c.as_string());
            }
        }
        ws->ws.ai_registry().set_model_capabilities(provider_id_utf8, caps);
        ws->ws.ai_registry().save();
        auto provider = ws->ws.ai_registry().find_provider(provider_id_utf8);
        xscope::utils::Json::Array out_caps;
        if (provider) {
            for (const auto& c : provider->model_capabilities) {
                out_caps.emplace_back(c);
            }
        }
        xscope::utils::Json::Object obj;
        obj.emplace("provider_id", std::string(provider_id_utf8));
        obj.emplace("model_capabilities", xscope::utils::Json(std::move(out_caps)));
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_search_modules_list(xscope_workspace* ws) {
    if (!ws) {
        return json_err("workspace is null");
    }
    try {
        ws->ws.ensure_search_providers();
        auto& reg = ws->ws.search_registry();
        xscope::utils::Json::Array modules;
        for (const auto& m : reg.list()) {
            bool secret_configured = true;
            if (m.requires_api_key && !m.auth.secret_id.empty()) {
                auto s = ws->ws.get_secret(m.auth.secret_id);
                secret_configured = s.has_value() && !s->empty();
            } else if (!m.requires_api_key) {
                secret_configured = true;
            }
            xscope::utils::Json::Object o;
            o.emplace("id", m.id);
            o.emplace("name", m.name);
            o.emplace("description", m.description);
            o.emplace("enabled", m.enabled);
            o.emplace("requires_api_key", m.requires_api_key);
            o.emplace("auth_type", std::string(xscope::registry::auth_type_to_string(m.auth.type)));
            o.emplace("secret_id", m.auth.secret_id);
            o.emplace("secret_configured", secret_configured);
            modules.emplace_back(std::move(o));
        }
        xscope::utils::Json::Object obj;
        obj.emplace("modules", xscope::utils::Json(std::move(modules)));
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_search_module_set_enabled(xscope_workspace* ws, const char* id_utf8, int enabled) {
    if (!ws) {
        return json_err("workspace is null");
    }
    if (!id_utf8 || !*id_utf8) {
        return json_err("id is empty");
    }
    try {
        ws->ws.ensure_search_providers();
        auto& reg = ws->ws.search_registry();
        if (!reg.find(id_utf8)) {
            return json_err(std::string("unknown search module: ") + id_utf8);
        }
        reg.set_enabled(id_utf8, enabled != 0);
        reg.save();
        xscope::utils::Json::Object obj;
        obj.emplace("id", std::string(id_utf8));
        obj.emplace("enabled", enabled != 0);
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_search_module_set_api_key(xscope_workspace* ws, const char* id_utf8,
                                       const char* api_key_utf8) {
    if (!ws) {
        return json_err("workspace is null");
    }
    if (!id_utf8 || !*id_utf8) {
        return json_err("id is empty");
    }
    if (!api_key_utf8 || !*api_key_utf8) {
        return json_err("api_key is empty");
    }
    try {
        ws->ws.ensure_search_providers();
        auto mod = ws->ws.search_registry().find(id_utf8);
        if (!mod) {
            return json_err(std::string("unknown search module: ") + id_utf8);
        }
        if (mod->auth.type == xscope::registry::AuthType::OAuth) {
            return json_err("module uses OAuth; use GitHub OAuth / PAT tools instead");
        }
        if (mod->auth.type != xscope::registry::AuthType::Bearer &&
            mod->auth.type != xscope::registry::AuthType::ApiKey) {
            return json_err("module does not accept an API key");
        }
        if (mod->auth.secret_id.empty()) {
            return json_err("module has empty secret_id");
        }
        ws->ws.put_secret(mod->auth.secret_id, mod->id, api_key_utf8);
        xscope::utils::Json::Object obj;
        obj.emplace("id", std::string(id_utf8));
        obj.emplace("secret_id", mod->auth.secret_id);
        obj.emplace("secret_configured", true);
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

namespace {

xscope::utils::Json::Object project_to_json(const xscope::storage::ProjectInfo& p) {
    xscope::utils::Json::Object o;
    o.emplace("id", p.id);
    o.emplace("title", p.title);
    o.emplace("created_at", p.created_at);
    o.emplace("updated_at", p.updated_at);
    o.emplace("path_rel", p.path_rel);
    o.emplace("pinned", p.pinned);
    return o;
}

} // namespace

char* xscope_project_list(xscope_workspace* ws) {
    if (!ws) {
        return json_err("workspace is null");
    }
    try {
        xscope::utils::Json::Array arr;
        for (const auto& p : ws->ws.list_projects()) {
            arr.emplace_back(project_to_json(p));
        }
        xscope::utils::Json::Object obj;
        obj.emplace("projects", xscope::utils::Json(std::move(arr)));
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_project_create(xscope_workspace* ws, const char* title_utf8) {
    if (!ws) {
        return json_err("workspace is null");
    }
    if (!title_utf8 || !*title_utf8) {
        return json_err("title is empty");
    }
    try {
        auto info = ws->ws.create_project(title_utf8);
        return json_ok(project_to_json(info));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_project_rename(xscope_workspace* ws, const char* id_utf8, const char* title_utf8) {
    if (!ws) {
        return json_err("workspace is null");
    }
    if (!id_utf8 || !*id_utf8) {
        return json_err("id is empty");
    }
    if (!title_utf8 || !*title_utf8) {
        return json_err("title is empty");
    }
    try {
        ws->ws.rename_project(id_utf8, title_utf8);
        auto info = ws->ws.get_project(id_utf8);
        if (!info) {
            return json_err("project missing after rename");
        }
        return json_ok(project_to_json(*info));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_project_set_pinned(xscope_workspace* ws, const char* id_utf8, int pinned) {
    if (!ws) {
        return json_err("workspace is null");
    }
    if (!id_utf8 || !*id_utf8) {
        return json_err("id is empty");
    }
    try {
        ws->ws.set_project_pinned(id_utf8, pinned != 0);
        auto info = ws->ws.get_project(id_utf8);
        if (!info) {
            return json_err("project missing after pin update");
        }
        return json_ok(project_to_json(*info));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_project_delete(xscope_workspace* ws, const char* id_utf8) {
    if (!ws) {
        return json_err("workspace is null");
    }
    if (!id_utf8 || !*id_utf8) {
        return json_err("id is empty");
    }
    try {
        ws->ws.delete_project(id_utf8);
        xscope::utils::Json::Object obj;
        obj.emplace("id", std::string(id_utf8));
        obj.emplace("deleted", true);
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_research_start(xscope_workspace* ws, const char* project_id_utf8,
                            const char* query_utf8, const char* model_id_utf8, int precision) {
    if (!ws || !ws->research) {
        return json_err("workspace is null");
    }
    if (!project_id_utf8 || !*project_id_utf8) {
        return json_err("project_id is empty");
    }
    if (!query_utf8 || !*query_utf8) {
        return json_err("query is empty");
    }
    try {
        const auto run_id = ws->research->start(project_id_utf8, query_utf8,
                                                model_id_utf8 ? model_id_utf8 : "",
                                                xscope::research::precision_from_int(precision));
        xscope::utils::Json::Object obj;
        obj.emplace("run_id", run_id);
        obj.emplace("project_id", std::string(project_id_utf8));
        obj.emplace("precision", precision);
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_research_continue(xscope_workspace* ws, const char* run_id_utf8,
                               const char* user_reply_utf8) {
    if (!ws || !ws->research) {
        return json_err("workspace is null");
    }
    if (!run_id_utf8 || !*run_id_utf8) {
        return json_err("run_id is empty");
    }
    try {
        ws->research->continue_with_user(run_id_utf8, user_reply_utf8 ? user_reply_utf8 : "");
        xscope::utils::Json::Object obj;
        obj.emplace("run_id", std::string(run_id_utf8));
        obj.emplace("continued", true);
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_research_cancel(xscope_workspace* ws, const char* run_id_utf8) {
    if (!ws || !ws->research) {
        return json_err("workspace is null");
    }
    if (!run_id_utf8 || !*run_id_utf8) {
        return json_err("run_id is empty");
    }
    try {
        ws->research->cancel(run_id_utf8);
        xscope::utils::Json::Object obj;
        obj.emplace("run_id", std::string(run_id_utf8));
        obj.emplace("cancelled", true);
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_research_poll_xaiop(xscope_workspace* ws, const char* run_id_utf8, int wait_ms) {
    if (!ws || !ws->research) {
        return json_err("workspace is null");
    }
    if (!run_id_utf8 || !*run_id_utf8) {
        return json_err("run_id is empty");
    }
    try {
        auto wire = ws->research->poll_xaiop(run_id_utf8, wait_ms);
        xscope::utils::Json::Object obj;
        obj.emplace("run_id", std::string(run_id_utf8));
        obj.emplace("wire", wire);
        obj.emplace("has_event", !wire.empty());
        if (!wire.empty()) {
            try {
                auto json_text = xscope::xaiop::Bridge::instance().parse_to_json(wire);
                obj.emplace("doc", xscope::utils::Json::parse(json_text));
            } catch (const std::exception& ex) {
                obj.emplace("doc_error", std::string(ex.what()));
            }
        }
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_research_status(xscope_workspace* ws, const char* run_id_utf8) {
    if (!ws || !ws->research) {
        return json_err("workspace is null");
    }
    if (!run_id_utf8 || !*run_id_utf8) {
        return json_err("run_id is empty");
    }
    try {
        auto st = ws->research->status(run_id_utf8);
        if (!st) {
            return json_err("unknown or inactive run_id");
        }
        xscope::utils::Json::Object obj;
        obj.emplace("run_id", st->id);
        obj.emplace("project_id", st->project_id);
        obj.emplace("query", st->query);
        obj.emplace("model_id", st->model_id);
        obj.emplace("precision", xscope::research::precision_to_int(st->precision));
        obj.emplace("precision_name", std::string(xscope::research::precision_to_string(st->precision)));
        obj.emplace("status", std::string(xscope::research::run_status_to_string(st->status)));
        obj.emplace("search_rounds_done", st->search_rounds_done);
        obj.emplace("summary", st->summary);
        obj.emplace("waiting_prompt", st->waiting_prompt);
        obj.emplace("error", st->last_error);
        obj.emplace("created_at", st->created_at);
        obj.emplace("updated_at", st->updated_at);
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_research_evidence_list(xscope_workspace* ws, const char* project_id_utf8,
                                    const char* run_id_utf8) {
    if (!ws || !ws->research) {
        return json_err("workspace is null");
    }
    if (!project_id_utf8 || !*project_id_utf8) {
        return json_err("project_id is empty");
    }
    if (!run_id_utf8 || !*run_id_utf8) {
        return json_err("run_id is empty");
    }
    try {
        auto items = ws->research->evidence_list(project_id_utf8, run_id_utf8);
        xscope::utils::Json::Array arr;
        for (const auto& e : items) {
            xscope::utils::Json::Object o;
            o.emplace("id", e.id);
            o.emplace("run_id", e.run_id);
            o.emplace("kind", e.kind);
            o.emplace("title", e.title);
            o.emplace("url", e.source_uri);
            o.emplace("source_uri", e.source_uri);
            o.emplace("module_id", e.module_id);
            o.emplace("snippet", e.snippet);
            o.emplace("round", e.round);
            o.emplace("created_at", e.created_at);
            arr.emplace_back(std::move(o));
        }
        xscope::utils::Json::Object obj;
        obj.emplace("run_id", std::string(run_id_utf8));
        obj.emplace("project_id", std::string(project_id_utf8));
        obj.emplace("items", xscope::utils::Json(std::move(arr)));
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_project_knowledge_graph(xscope_workspace* ws, const char* project_id_utf8) {
    if (!ws) {
        return json_err("workspace is null");
    }
    if (!project_id_utf8 || !*project_id_utf8) {
        return json_err("project_id is empty");
    }
    try {
        auto db = ws->ws.open_project_db(project_id_utf8);
        xscope::research::KnowledgeGraphStore kg;
        kg.open(db);
        auto graph = kg.graph_json(project_id_utf8);
        kg.close();
        db.close();
        xscope::utils::Json::Object obj;
        obj.emplace("graph", std::move(graph));
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

char* xscope_project_research_snapshot(xscope_workspace* ws, const char* project_id_utf8) {
    if (!ws) {
        return json_err("workspace is null");
    }
    if (!project_id_utf8 || !*project_id_utf8) {
        return json_err("project_id is empty");
    }
    try {
        const std::string project_id = project_id_utf8;
        auto db = ws->ws.open_project_db(project_id);
        xscope::research::EvidenceStore store;
        const auto files =
            xscope::utils::path_from_utf8(ws->ws.data_root()) / "projects" / project_id / "files";
        store.open(db, files);

        xscope::utils::Json::Object obj;
        obj.emplace("project_id", project_id);

        auto runs = store.list_runs();
        std::string run_id;
        std::string query;
        std::string summary;
        std::string status;
        std::string model_id;
        int precision = 1;
        if (!runs.empty()) {
            const auto& run = runs.front(); // created_at DESC
            run_id = run.id;
            query = run.query;
            summary = run.summary;
            status = xscope::research::run_status_to_string(run.status);
            model_id = run.model_id;
            precision = xscope::research::precision_to_int(run.precision);

            xscope::utils::Json::Object run_obj;
            run_obj.emplace("run_id", run.id);
            run_obj.emplace("query", run.query);
            run_obj.emplace("model_id", run.model_id);
            run_obj.emplace("precision", precision);
            run_obj.emplace("status", status);
            run_obj.emplace("summary", run.summary);
            run_obj.emplace("search_rounds_done", run.search_rounds_done);
            run_obj.emplace("created_at", run.created_at);
            run_obj.emplace("updated_at", run.updated_at);
            obj.emplace("run", xscope::utils::Json(std::move(run_obj)));
        }

        std::string report_markdown;
        xscope::research::MemoryTreeStore mem;
        mem.open(db);
        // Only need report bodies — avoid pulling every memory row body when possible.
        auto entries = mem.list_entries(project_id);
        std::int64_t best_ts = -1;
        for (const auto& e : entries) {
            if (e.kind != "report") {
                continue;
            }
            if (e.updated_at >= best_ts) {
                best_ts = e.updated_at;
                report_markdown = e.body;
            }
        }
        mem.close();

        int event_count = 0;
        int evidence_count = 0;
        if (!runs.empty()) {
            evidence_count = runs.front().search_rounds_done;
        }
        if (!run_id.empty()) {
            event_count = store.count_events(run_id);
            // Do NOT ship / load full event payloads when a report already exists
            // (replaying them froze the WPF feed). Only peek synthesize/final as fallback.
            if (report_markdown.empty()) {
                auto events = store.list_events(run_id, 0);
                for (auto it = events.rbegin(); it != events.rend(); ++it) {
                    if (it->phase != "synthesize" && it->phase != "final") {
                        continue;
                    }
                    try {
                        auto doc = xscope::utils::Json::parse(it->payload_json);
                        if (doc.is_object() && doc.contains("payload")) {
                            const auto& p = doc.at("payload");
                            if (p.is_object() && p.contains("markdown")) {
                                report_markdown = p.at("markdown").as_string("");
                                if (!report_markdown.empty()) {
                                    break;
                                }
                            }
                        }
                    } catch (...) {
                    }
                }
            }
        }

        obj.emplace("events", xscope::utils::Json(xscope::utils::Json::Array{}));
        obj.emplace("event_count", static_cast<std::int64_t>(event_count));
        obj.emplace("evidence_count", static_cast<std::int64_t>(evidence_count));
        obj.emplace("report_markdown", report_markdown);
        obj.emplace("has_report", !report_markdown.empty());
        obj.emplace("query", query);
        obj.emplace("summary", summary);
        obj.emplace("status", status);
        obj.emplace("model_id", model_id);
        obj.emplace("precision", precision);

        xscope::research::KnowledgeGraphStore kg;
        kg.open(db);
        obj.emplace("knowledge_graph", kg.graph_json(project_id));
        kg.close();

        store.close();
        db.close();
        return json_ok(std::move(obj));
    } catch (const std::exception& ex) {
        return json_err(ex.what());
    }
}

} // extern "C"
