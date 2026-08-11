#include "xscope/xscope.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    try {
        auto& xaiop = xscope::xaiop::Bridge::instance();
        std::cout << "XAIOP SDK " << xaiop.sdk_version()
                  << " / protocol " << xaiop.protocol_version() << '\n';

        // --- storage smoke ---
        const auto root = fs::path("D:/Project/XScope/sdk/out/smoke-data");
        fs::remove_all(root);
        fs::create_directories(root);

        xscope::storage::Workspace ws;
        ws.open(root.string());
        ws.put_secret("serp.default", "serpapi", "sk-test-secret-value");
        const auto secret = ws.get_secret("serp.default");
        if (!secret || *secret != "sk-test-secret-value") {
            throw std::runtime_error("secret round-trip failed");
        }
        ws.create_project("First research");
        const std::string history_wire = ws.projects_history_xaiop();
        std::cout << "history wire bytes: " << history_wire.size() << '\n';

        // --- skills (file-based) ---
        ws.skills().save("demo-research",
                         "---\nname: demo-research\ndescription: Smoke skill for network research\n---\n\n"
                         "# Demo\n\nUse this skill when researching.\n");
        // github-search (builtin) + demo-research
        if (ws.skills().list().size() != 2) {
            throw std::runtime_error("skill list size mismatch");
        }
        const auto skill_doc = ws.skills().load("demo-research");
        if (skill_doc.info.name != "demo-research" || skill_doc.body.find("Demo") == std::string::npos) {
            throw std::runtime_error("skill load mismatch");
        }
        const std::string skill_wire = ws.skills().catalog_xaiop();
        std::cout << "skill catalog wire bytes: " << skill_wire.size() << '\n';

        // --- utils ---
        if (xscope::utils::json_escape("a\"b") != "a\\\"b") {
            throw std::runtime_error("utils json_escape failed");
        }
        if (xscope::utils::sanitize_id("Hello World!") != "Hello-World") {
            throw std::runtime_error("utils sanitize_id failed");
        }
        if (xscope::utils::url_encode("a b/c") != "a%20b%2Fc") {
            throw std::runtime_error("utils url_encode failed");
        }
        if (xscope::utils::base64_decode_string("SGVsbG8=") != "Hello") {
            throw std::runtime_error("utils base64_decode failed");
        }
        {
            const auto links = xscope::providers::github::RestClient::parse_link_header(
                R"(<https://api.github.com/resource?page=2>; rel="next", )"
                R"(<https://api.github.com/resource?page=5>; rel="last")");
            if (links.next.find("page=2") == std::string::npos ||
                links.last.find("page=5") == std::string::npos) {
                throw std::runtime_error("Link header parse failed");
            }
        }

        // Builtin GitHub provider seeded on open.
        if (!ws.skills().find("github-search")) {
            throw std::runtime_error("github-search skill missing");
        }
        if (!ws.search_registry().find("github")) {
            throw std::runtime_error("github registry module missing");
        }

        // --- search module registry ---
        {
            xscope::registry::SearchModule mod;
            mod.id = "demo-search";
            mod.name = "Demo Search";
            mod.description = "Static smoke search module";
            mod.enabled = true;
            mod.skill_id = "demo-research";
            mod.requires_api_key = true;
            mod.auth.type = xscope::registry::AuthType::ApiKey;
            mod.auth.secret_id = "serp.default";
            mod.auth.param_name = "api_key";
            mod.tags = {"smoke", "demo"};
            ws.search_registry().upsert(mod);
            ws.search_registry().save();

            // Simulate client static JSON import / merge.
            const auto static_json = root / "static-search-modules.json";
            xscope::utils::write_file_utf8(static_json, R"({
  "schema": 1,
  "modules": [
    {
      "id": "static-bing-like",
      "name": "Static Provider",
      "description": "Provided by client package",
      "enabled": false,
      "skill_id": "demo-research",
      "requires_api_key": false,
      "auth": { "type": "none", "secret_id": "", "param_name": "" },
      "tags": ["static"]
    }
  ]
})");
            ws.search_registry().merge_file(static_json);
            ws.search_registry().save();

            // Structural validation (github.oauth may be absent until OAuth/PAT).
            auto check = ws.search_registry().validate_all(
                &ws.skills(), false, [&](const std::string& sid) { return ws.get_secret(sid).has_value(); });
            if (!check.ok) {
                throw std::runtime_error("registry validation failed");
            }
            auto demo_check = ws.search_registry().validate(
                *ws.search_registry().find("demo-search"), &ws.skills(), true,
                [&](const std::string& sid) { return ws.get_secret(sid).has_value(); });
            if (!demo_check.ok) {
                throw std::runtime_error("demo-search secret validation failed");
            }
            // github (builtin) + demo-search + static-bing-like
            if (ws.search_registry().list().size() != 3) {
                throw std::runtime_error("registry size mismatch");
            }
            const auto reg_wire = ws.search_registry().catalog_xaiop();
            std::cout << "search registry wire bytes: " << reg_wire.size() << '\n';

            const auto usable = ws.list_usable_search_modules(true);
            if (usable.size() != 1) {
                throw std::runtime_error("usable module count mismatch");
            }
            if (usable[0].skill.raw.find("Use this skill when researching") == std::string::npos) {
                throw std::runtime_error("usable module missing full skill text");
            }

            const auto system_prompt = ws.render_chat_system_prompt(true);
            if (system_prompt.find("{{search_modules}}") != std::string::npos) {
                throw std::runtime_error("search_modules placeholder was not injected");
            }
            if (system_prompt.find(usable[0].skill.raw) == std::string::npos) {
                throw std::runtime_error("system prompt lost skill fidelity");
            }
            std::cout << "chat system prompt bytes: " << system_prompt.size() << '\n';

            auto tools = ws.search_tools(true);
            const auto list_resp = tools.call({xscope::mcp::SearchToolService::kListSearchModules,
                                               xscope::utils::Json(xscope::utils::Json::Object{})});
            if (!list_resp.ok) {
                throw std::runtime_error(list_resp.error);
            }
            const auto skill_resp = tools.call(
                {xscope::mcp::SearchToolService::kGetSearchModuleSkill,
                 xscope::utils::Json::parse(R"({"module_id":"demo-search"})")});
            if (!skill_resp.ok) {
                throw std::runtime_error(skill_resp.error);
            }
            if (skill_resp.result.at("skill").at("raw").as_string().find("Demo") == std::string::npos) {
                throw std::runtime_error("MCP get_search_module_skill lost skill text");
            }

            // GitHub OAuth status (disconnected) + PAT fallback store (no live login required).
            const auto gh_status = tools.call({xscope::mcp::SearchToolService::kGithubOAuthStatus,
                                               xscope::utils::Json(xscope::utils::Json::Object{})});
            if (!gh_status.ok || gh_status.result.at("connected").as_bool(true)) {
                throw std::runtime_error("github oauth should start disconnected");
            }
            const auto pat_resp = tools.call(
                {xscope::mcp::SearchToolService::kGithubOAuthSetPat,
                 xscope::utils::Json::parse(R"({"token":"ghp_smoke_test_token_not_real","scope":"read:user"})")});
            if (!pat_resp.ok || !pat_resp.result.at("connected").as_bool(false)) {
                throw std::runtime_error("github oauth set_pat failed");
            }
            // Do not call refresh /user with fake token — set_pat best-efforts /user and keeps token.
            if (!ws.get_secret("github.oauth")) {
                throw std::runtime_error("github.oauth secret missing after set_pat");
            }
            const auto disc = tools.call({xscope::mcp::SearchToolService::kGithubOAuthDisconnect,
                                          xscope::utils::Json(xscope::utils::Json::Object{})});
            if (!disc.ok || disc.result.at("connected").as_bool(true)) {
                throw std::runtime_error("github oauth disconnect failed");
            }

            const auto catalog = tools.call({xscope::mcp::SearchToolService::kGithubRestCatalog,
                                             xscope::utils::Json(xscope::utils::Json::Object{})});
            if (!catalog.ok || !catalog.result.contains("resources") ||
                catalog.result.at("resources").as_array().size() < 20) {
                throw std::runtime_error("github_rest_catalog incomplete");
            }

            std::cout << "mcp tools ok, descriptors=" << tools.descriptors().size() << '\n';
        }

        // --- AI registry / XAIOP contract ---
        {
            if (ws.ai_registry().list_providers().size() < 2) {
                throw std::runtime_error("AI builtin providers missing");
            }
            auto ai = ws.ai_runtime();
            const auto catalog = ai.catalog_xaiop();
            if (catalog.empty()) {
                throw std::runtime_error("AI catalog_xaiop empty");
            }
            xscope::xaiop::LiveParser live_ai;
            live_ai.feed_text(catalog);
            const auto snap = live_ai.snapshot_json();
            if (snap.find("ai_provider_registry") == std::string::npos) {
                throw std::runtime_error("AI catalog_xaiop did not materialize registry kind");
            }
            const auto usable = ai.list_usable_models_xaiop(false);
            xscope::xaiop::LiveParser live_usable;
            live_usable.feed_text(usable);
            if (live_usable.snapshot_json().find("ai_usable_models") == std::string::npos) {
                throw std::runtime_error("AI usable models XAIOP kind mismatch");
            }
            std::cout << "ai catalog wire bytes: " << catalog.size()
                      << " usable wire bytes: " << usable.size() << '\n';

            // Optional: refresh models + live chat when secret present.
            if (ws.get_secret("deepseek.default")) {
                try {
                    const auto refreshed = ai.refresh_models_from_api("deepseek");
                    if (!refreshed.contains("models") || refreshed.at("models").as_array().empty()) {
                        throw std::runtime_error("deepseek refresh_models returned empty");
                    }
                    const auto preferred = refreshed.at("preferred_model_id").as_string("");
                    if (preferred.empty()) {
                        throw std::runtime_error("deepseek preferred_model_id empty after refresh");
                    }
                    xscope::ai::ChatRequest req;
                    req.model_id = preferred;
                    req.stream_id = "smoke";
                    req.messages.push_back({"user", "Reply with exactly: pong", "", ""});
                    int phases = 0;
                    const auto final_wire = ai.chat_stream_xaiop(
                        req, [&](const std::string& wire, bool) {
                            ++phases;
                            xscope::xaiop::LiveParser lp;
                            lp.feed_text(wire);
                            if (lp.snapshot_json().find("ai_chat") == std::string::npos) {
                                throw std::runtime_error("AI stream phase not ai_chat XAIOP");
                            }
                        });
                    if (phases < 1 || final_wire.empty()) {
                        throw std::runtime_error("AI stream produced no XAIOP phases");
                    }
                    std::cout << "ai stream phases=" << phases
                              << " final wire bytes=" << final_wire.size() << '\n';
                } catch (const std::exception& ex) {
                    std::cout << "ai live chat skipped: " << ex.what() << '\n';
                }
            }
        }

        // --- network + optional XAIOP ---
        xscope::network::ClientOptions net_opts;
        net_opts.enable_xaiop = true;
        xscope::network::NetworkClient net(net_opts);

        const std::string sample_json = R"({"meta":{"kind":"ping"},"ok":true})";
        const std::string encoded = net.xaiop().encode_json(sample_json);
        std::cout << "--- encoded XAIOP ---\n" << encoded;

        xscope::xaiop::LiveParser live;
        live.feed_text(encoded);
        std::cout << "--- live snapshot ---\n" << live.snapshot_json() << '\n';

        // Generic HTTP (no XAIOP required on the wire). Soft-fail if offline.
        try {
            xscope::network::HttpRequest req;
            req.method = xscope::network::HttpMethod::Get;
            req.url = "https://example.com";
            req.timeout = std::chrono::seconds(10);
            req.connect_timeout = std::chrono::seconds(5);
            auto resp = net.http().send(req);
            std::cout << "HTTP GET example.com => " << resp.status
                      << " body=" << resp.body.size() << " bytes\n";
        } catch (const std::exception& ex) {
            std::cout << "HTTP smoke skipped: " << ex.what() << '\n';
        }

        std::cout << "OK\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "ERROR: " << ex.what() << '\n';
        return 1;
    }
}
