#include "xscope/research/orchestrator.hpp"

#include "xscope/ai/types.hpp"
#include "xscope/mcp/search_tools.hpp"
#include "xscope/mcp/tool_types.hpp"
#include "xscope/prompts/prompt_engine.hpp"
#include "xscope/registry/usable_module.hpp"
#include "xscope/utils/path.hpp"
#include "xscope/utils/time.hpp"
#include "xscope/xaiop/bridge.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <random>
#include <sstream>
#include <stdexcept>

namespace xscope::research {
namespace {

std::string extract_json_blob(const std::string& text) {
    const auto start = text.find('{');
    const auto end = text.rfind('}');
    if (start == std::string::npos || end == std::string::npos || end <= start) {
        return {};
    }
    return text.substr(start, end - start + 1);
}

void fill_github_rest_evidence(const std::string& path, const utils::Json& result, bool ok,
                               std::string* title, std::string* snippet);
bool has_github_repo_evidence(const std::vector<EvidenceItem>& memory);
std::string truncate_utf8(std::string s, std::size_t max_bytes);
std::string extract_github_owner(const std::string& blob);
std::string extract_github_repo(const std::string& blob);
std::string build_github_facts_report(const std::string& need, const std::string& query,
                                      const std::vector<EvidenceItem>& memory);
std::string build_generic_evidence_report(const std::string& need, const std::string& query,
                                          const std::vector<EvidenceItem>& memory, int github_fail_streak);

} // namespace

struct ResearchOrchestrator::ResearchDirection {
    std::string id;
    std::string label;
    int depth = 0;
    bool closed = false;
};

struct ResearchOrchestrator::ActiveRun {
    ResearchRun run;
    ResearchBudget budget;
    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::string> phases;
    bool cancel = false;
    bool has_user_reply = false;
    std::string user_reply;
    std::thread worker;
    std::vector<EvidenceItem> memory;
    std::string clarified_query;
    std::string dialogue;
    int discovery_ai_turns = 0;
    int discovery_searches = 0;
    int discovery_asks = 0;
    int searches_since_ask = 0;
    // Stage 2 — deep research
    bool stage_research = false;
    bool prefer_github = false;
    std::string preferred_module = "bocha";
    std::string preferred_endpoint = "web-search";
    std::vector<ResearchDirection> directions;
    int research_ai_turns = 0;
    int research_asks = 0;
    int github_fail_streak = 0;
    bool github_direct_tried = false;
    std::string report_markdown;
    std::string memory_branch_id;
    std::string memory_tip_id;
};

ResearchOrchestrator::ResearchOrchestrator(storage::Workspace& workspace) : ws_(workspace) {}

ResearchOrchestrator::~ResearchOrchestrator() {
    std::vector<std::shared_ptr<ActiveRun>> copy;
    {
        std::lock_guard lock(map_mu_);
        for (auto& [_, a] : active_) {
            {
                std::lock_guard lk(a->mu);
                a->cancel = true;
                a->has_user_reply = true;
            }
            a->cv.notify_all();
            copy.push_back(a);
        }
        active_.clear();
    }
    for (auto& a : copy) {
        if (a->worker.joinable()) {
            a->worker.join();
        }
    }
}

std::string ResearchOrchestrator::make_id(const char* prefix) {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<std::uint64_t> dist;
    std::ostringstream oss;
    oss << prefix << std::hex << dist(rng);
    return oss.str();
}

std::string ResearchOrchestrator::start(const std::string& project_id, const std::string& query,
                                        const std::string& model_id, PrecisionKind precision) {
    if (!ws_.get_project(project_id)) {
        throw std::runtime_error("unknown project id");
    }
    auto active = std::make_shared<ActiveRun>();
    active->budget = budget_for(precision);
    active->run.id = make_id("run_");
    active->run.project_id = project_id;
    active->run.query = query;
    active->run.model_id = model_id;
    active->run.precision = precision;
    active->run.status = RunStatus::Running;
    active->run.created_at = utils::now_unix_seconds();
    active->run.updated_at = active->run.created_at;
    active->clarified_query = query;

    {
        auto db = ws_.open_project_db(project_id);
        EvidenceStore store;
        const auto files = utils::path_from_utf8(ws_.data_root()) / "projects" / project_id / "files";
        store.open(db, files);
        store.upsert_run(active->run);
        store.close();
        db.close();
    }

    {
        std::lock_guard lock(map_mu_);
        active_[active->run.id] = active;
    }

    emit(*active, "start", utils::Json(utils::Json::Object{
                               {"message", std::string("requirements discovery started")},
                               {"stage", std::string("requirements")},
                               {"budget_max_depth_layers", static_cast<std::int64_t>(active->budget.max_depth_layers)},
                               {"budget_max_directions", static_cast<std::int64_t>(active->budget.max_directions)},
                               {"items_per_layer", static_cast<std::int64_t>(active->budget.items_per_layer)},
                           }));

    active->worker = std::thread([this, active]() { worker_main(active); });
    return active->run.id;
}

void ResearchOrchestrator::continue_with_user(const std::string& run_id, const std::string& user_reply) {
    std::shared_ptr<ActiveRun> active;
    {
        std::lock_guard lock(map_mu_);
        auto it = active_.find(run_id);
        if (it == active_.end()) {
            throw std::runtime_error("unknown or inactive run_id");
        }
        active = it->second;
    }
    {
        std::lock_guard lk(active->mu);
        active->user_reply = user_reply;
        active->has_user_reply = true;
        if (active->run.status == RunStatus::WaitingUser) {
            active->run.status = RunStatus::Running;
            active->run.waiting_prompt.clear();
        }
    }
    active->cv.notify_all();
}

void ResearchOrchestrator::cancel(const std::string& run_id) {
    std::shared_ptr<ActiveRun> active;
    {
        std::lock_guard lock(map_mu_);
        auto it = active_.find(run_id);
        if (it == active_.end()) {
            return;
        }
        active = it->second;
    }
    {
        std::lock_guard lk(active->mu);
        active->cancel = true;
        active->has_user_reply = true;
        active->run.status = RunStatus::Cancelled;
    }
    active->cv.notify_all();
}

std::string ResearchOrchestrator::poll_xaiop(const std::string& run_id, int wait_ms) {
    std::shared_ptr<ActiveRun> active;
    {
        std::lock_guard lock(map_mu_);
        auto it = active_.find(run_id);
        if (it == active_.end()) {
            return {};
        }
        active = it->second;
    }
    std::unique_lock lk(active->mu);
    if (active->phases.empty() && wait_ms > 0) {
        active->cv.wait_for(lk, std::chrono::milliseconds(wait_ms),
                            [&] { return !active->phases.empty() || active->cancel; });
    }
    if (active->phases.empty()) {
        return {};
    }
    auto wire = std::move(active->phases.front());
    active->phases.pop_front();
    return wire;
}

std::optional<ResearchRun> ResearchOrchestrator::status(const std::string& run_id) {
    {
        std::lock_guard lock(map_mu_);
        auto it = active_.find(run_id);
        if (it != active_.end()) {
            std::lock_guard lk(it->second->mu);
            return it->second->run;
        }
    }
    // Fall back to last known project DBs is expensive; require active for status in v1.
    return std::nullopt;
}

std::vector<EvidenceItem> ResearchOrchestrator::evidence_list(const std::string& project_id,
                                                              const std::string& run_id) {
    {
        std::lock_guard lock(map_mu_);
        auto it = active_.find(run_id);
        if (it != active_.end()) {
            std::lock_guard lk(it->second->mu);
            return it->second->memory;
        }
    }
    auto db = ws_.open_project_db(project_id);
    EvidenceStore store;
    const auto files = utils::path_from_utf8(ws_.data_root()) / "projects" / project_id / "files";
    store.open(db, files);
    auto items = store.list_evidence(run_id);
    store.close();
    db.close();
    return items;
}

void ResearchOrchestrator::emit(ActiveRun& active, const std::string& phase, const utils::Json& payload) {
    active.run.updated_at = utils::now_unix_seconds();
    auto doc = make_phase_doc(active.run, phase, payload);
    auto wire = xaiop::Bridge::instance().encode_json(doc.dump(0));
    {
        std::lock_guard lk(active.mu);
        active.phases.push_back(wire);
    }
    active.cv.notify_all();

    try {
        auto db = ws_.open_project_db(active.run.project_id);
        EvidenceStore store;
        const auto files =
            utils::path_from_utf8(ws_.data_root()) / "projects" / active.run.project_id / "files";
        store.open(db, files);
        store.append_event(active.run.id, phase, doc.dump(0));
        store.upsert_run(active.run);
        store.close();
        db.close();
    } catch (...) {
        // Persistence failure must not kill the UI stream.
    }
}

void ResearchOrchestrator::persist(ActiveRun& active) {
    active.run.updated_at = utils::now_unix_seconds();
    auto db = ws_.open_project_db(active.run.project_id);
    EvidenceStore store;
    const auto files =
        utils::path_from_utf8(ws_.data_root()) / "projects" / active.run.project_id / "files";
    store.open(db, files);
    store.upsert_run(active.run);
    for (const auto& e : active.memory) {
        store.upsert_evidence(e);
    }
    store.close();
    db.close();
}

std::string ResearchOrchestrator::build_system_prompt(ActiveRun& active) {
    prompts::PromptContext ctx;
    ctx.usable_modules = ws_.list_usable_search_modules(false);
    ctx.extras["research_policy"] = policy_prompt_text(active.budget);
    ctx.extras["query"] = active.clarified_query.empty() ? active.run.query : active.clarified_query;
    ctx.extras["evidence_index"] = evidence_index_text(active);
    try {
        return ws_.prompts().render("research_system", ctx, true);
    } catch (...) {
        // Fallback if template missing.
        std::ostringstream oss;
        oss << "You are XScope research orchestrator assistant.\n\n"
            << ctx.extras["research_policy"] << "\n"
            << "## Search modules\n{{search_modules}}\n";
        auto block = registry::format_usable_modules_for_prompt(ctx.usable_modules);
        auto templ = oss.str();
        auto pos = templ.find("{{search_modules}}");
        if (pos != std::string::npos) {
            templ.replace(pos, std::string("{{search_modules}}").size(), block);
        }
        return templ;
    }
}

std::string ResearchOrchestrator::evidence_index_text(ActiveRun& active) {
    std::ostringstream oss;
    oss << "Dialogue:\n" << (active.dialogue.empty() ? "(none)\n" : active.dialogue) << "\n";
    oss << "Discovery hits (" << active.memory.size() << "):\n";
    for (const auto& e : active.memory) {
        oss << "- [" << e.id << "] q-round=" << e.round << " module=" << e.module_id << " "
            << e.title << " | " << e.source_uri << "\n  " << e.snippet << "\n";
        // Surface a short body preview so synthesize can use REST facts (not just "REST OK").
        if (!e.body_json.empty() && e.module_id == "github" && e.body_json.size() > 8) {
            auto preview = e.body_json;
            if (preview.size() > 280) {
                preview = preview.substr(0, 280) + "…";
            }
            oss << "  body: " << preview << "\n";
        }
    }
    if (active.memory.empty()) {
        oss << "(none yet)\n";
    }
    return oss.str();
}

utils::Json ResearchOrchestrator::try_parse_json_object(const std::string& text) {
    auto blob = extract_json_blob(text);
    if (blob.empty()) {
        return utils::Json(nullptr);
    }
    try {
        auto j = utils::Json::parse(blob);
        if (j.is_object()) {
            return j;
        }
    } catch (...) {
    }
    return utils::Json(nullptr);
}

std::string ResearchOrchestrator::ask_ai_json(ActiveRun& active, const std::string& system,
                                              const std::string& user) {
    if (active.run.model_id.empty()) {
        return {};
    }
    try {
        auto ai = ws_.ai_runtime();
        ai::ChatRequest req;
        req.model_id = active.run.model_id;
        req.stream = false;
        req.temperature = 0.3;
        req.messages.push_back(ai::ChatMessage{"system", system, "", ""});
        req.messages.push_back(ai::ChatMessage{"user", user, "", ""});
        auto wire = ai.chat_xaiop(req);
        auto json_text = xaiop::Bridge::instance().parse_to_json(wire);
        auto doc = utils::Json::parse(json_text);
        if (doc.contains("assistant") && doc.at("assistant").is_object()) {
            return doc.at("assistant").at("content").as_string("");
        }
    } catch (...) {
        // AI optional; orchestrator continues with heuristics.
    }
    return {};
}

void ResearchOrchestrator::normalize_search_target(std::string* module_id, std::string* endpoint) {
    if (!module_id || !endpoint) {
        return;
    }
    if (*module_id != "github" && *module_id != "bocha") {
        *module_id = "bocha";
    }
    if (*module_id == "github") {
        if (*endpoint != "repositories" && *endpoint != "code" && *endpoint != "issues" &&
            *endpoint != "commits" && *endpoint != "users" && *endpoint != "topics" &&
            *endpoint != "labels") {
            *endpoint = "repositories";
        }
    } else if (*endpoint != "web-search" && *endpoint != "ai-search") {
        *endpoint = "web-search";
    }
}

bool ResearchOrchestrator::wait_user_reply(ActiveRun& active, std::string* out_reply) {
    // Do NOT hold mu across long waits incorrectly — unique_lock is released inside wait().
    std::unique_lock lk(active.mu);
    active.cv.wait(lk, [&] { return active.cancel || active.has_user_reply; });
    if (active.cancel) {
        return false;
    }
    if (out_reply) {
        *out_reply = active.user_reply;
    }
    active.has_user_reply = false;
    active.user_reply.clear();
    active.run.status = RunStatus::Running;
    active.run.waiting_prompt.clear();
    return true;
}

void ResearchOrchestrator::run_search_round(ActiveRun& active, const std::string& module_id,
                                            const std::string& endpoint, const std::string& q,
                                            const std::string& purpose) {
    std::string mid = module_id;
    std::string ep = endpoint;
    std::string q_clean = humanize_user_reply(q);
    if (q_clean.empty()) {
        q_clean = q;
    }

    // GitHub-shaped needs: do not fall back to generic web search (noise / wrong hits).
    if (active.prefer_github && mid == "bocha") {
        emit(active, "thinking",
             utils::Json(utils::Json::Object{
                 {"text",
                  std::string("当前为 GitHub 定向需求，跳过网页搜索兜底，改用 GitHub REST 直查。")},
                 {"stage", purpose == "research" ? std::string("research")
                                                 : std::string("requirements")},
             }));
        try_github_direct_lookup(active);
        return;
    }

    // After Search API failures, stop retrying the same Search path — use REST instead.
    if (mid == "github" && active.github_fail_streak >= 1) {
        emit(active, "thinking",
             utils::Json(utils::Json::Object{
                 {"text",
                  std::string("已跳过重复的 GitHub Search（此前已失败）。继续用 REST 直查。")},
                 {"stage", purpose == "research" ? std::string("research")
                                                 : std::string("requirements")},
             }));
        try_github_direct_lookup(active);
        return;
    }

    const int round = active.run.search_rounds_done + 1;
    const int count = std::min(std::max(1, active.budget.items_per_layer), 20);

    utils::Json::Object keyword_payload;
    keyword_payload.emplace("keyword", q_clean);
    keyword_payload.emplace("module_id", mid);
    keyword_payload.emplace("endpoint", ep);
    keyword_payload.emplace("purpose", purpose);
    keyword_payload.emplace("round", round);
    emit(active, "keyword", utils::Json(keyword_payload));

    utils::Json::Object searching;
    searching.emplace("round", round);
    searching.emplace("module_id", mid);
    searching.emplace("endpoint", ep);
    searching.emplace("q", q_clean);
    searching.emplace("keyword", q_clean);
    searching.emplace("purpose", purpose);
    searching.emplace("count", count);
    emit(active, "searching", utils::Json(std::move(searching)));

    mcp::ToolRequest treq;
    treq.name = mcp::SearchToolService::kRunSearch;
    utils::Json::Object args;
    args.emplace("module_id", mid);
    args.emplace("endpoint", ep);
    args.emplace("q", q_clean);
    args.emplace("count", static_cast<std::int64_t>(count));
    if (mid == "bocha" && ep == "web-search") {
        args.emplace("summary", true);
    }
    if (mid == "bocha" && ep == "ai-search") {
        args.emplace("answer", false);
        args.emplace("stream", false);
    }
    treq.arguments = utils::Json(std::move(args));

    auto tools = ws_.search_tools(false);
    auto resp = tools.call(treq);
    active.run.search_rounds_done = round;
    active.discovery_searches += 1;

    utils::Json::Array hits;
    if (resp.ok && resp.result.is_object()) {
        try {
            const auto& response = resp.result.at("response");
            if (response.contains("body") && response.at("body").is_object()) {
                const auto& body = response.at("body");
                const utils::Json* pages_root = &body;
                if (body.contains("data") && body.at("data").is_object()) {
                    pages_root = &body.at("data");
                }
                if (pages_root->contains("webPages") && pages_root->at("webPages").is_object() &&
                    pages_root->at("webPages").contains("value") &&
                    pages_root->at("webPages").at("value").is_array()) {
                    for (const auto& v : pages_root->at("webPages").at("value").as_array()) {
                        if (!v.is_object()) {
                            continue;
                        }
                        if (static_cast<int>(hits.size()) >= count) {
                            break;
                        }
                        EvidenceItem e;
                        e.id = make_id("ev_");
                        e.run_id = active.run.id;
                        e.kind = "web";
                        e.module_id = mid;
                        e.title = v.contains("name") ? v.at("name").as_string("") : "";
                        e.source_uri = v.contains("url") ? v.at("url").as_string("") : "";
                        e.snippet = v.contains("summary") ? v.at("summary").as_string("")
                                    : v.contains("snippet") ? v.at("snippet").as_string("")
                                                         : "";
                        e.body_json = v.dump(0);
                        e.round = round;
                        e.created_at = utils::now_unix_seconds();
                        active.memory.push_back(e);
                        utils::Json::Object h;
                        h.emplace("evidence_id", e.id);
                        h.emplace("title", e.title);
                        h.emplace("url", e.source_uri);
                        h.emplace("snippet", e.snippet);
                        h.emplace("keyword", q_clean);
                        h.emplace("round", round);
                        h.emplace("module_id", mid);
                        hits.emplace_back(std::move(h));
                    }
                }
                if (body.contains("items") && body.at("items").is_array()) {
                    for (const auto& v : body.at("items").as_array()) {
                        if (!v.is_object()) {
                            continue;
                        }
                        if (static_cast<int>(hits.size()) >= count) {
                            break;
                        }
                        EvidenceItem e;
                        e.id = make_id("ev_");
                        e.run_id = active.run.id;
                        e.kind = "github";
                        e.module_id = mid;
                        e.title = v.contains("full_name") ? v.at("full_name").as_string("")
                                  : v.contains("name")      ? v.at("name").as_string("")
                                  : v.contains("title")     ? v.at("title").as_string("")
                                                          : "";
                        e.source_uri = v.contains("html_url") ? v.at("html_url").as_string("") : "";
                        e.snippet = v.contains("description") ? v.at("description").as_string("") : "";
                        e.body_json = v.dump(0);
                        e.round = round;
                        e.created_at = utils::now_unix_seconds();
                        active.memory.push_back(e);
                        utils::Json::Object h;
                        h.emplace("evidence_id", e.id);
                        h.emplace("title", e.title);
                        h.emplace("url", e.source_uri);
                        h.emplace("snippet", e.snippet);
                        h.emplace("keyword", q_clean);
                        h.emplace("round", round);
                        h.emplace("module_id", mid);
                        hits.emplace_back(std::move(h));
                    }
                }
            }
        } catch (...) {
        }
    }

    utils::Json::Object ev_payload;
    ev_payload.emplace("round", round);
    ev_payload.emplace("keyword", q_clean);
    ev_payload.emplace("purpose", purpose);
    ev_payload.emplace("ok", resp.ok);
    if (!resp.ok) {
        ev_payload.emplace("error", resp.error);
        if (mid == "github") {
            active.github_fail_streak += 1;
            emit(active, "thinking",
                 utils::Json(utils::Json::Object{
                     {"text",
                      std::string("GitHub 搜索失败（") + resp.error +
                          "）。将改用已认证 REST 直查用户/仓库，避免重复无效搜索。"},
                     {"stage", purpose == "research" ? std::string("research")
                                                     : std::string("requirements")},
                 }));
            if (active.prefer_github || purpose == "research" || purpose == "requirements") {
                try_github_direct_lookup(active);
            }
        }
    } else if (mid == "github") {
        active.github_fail_streak = 0;
    }
    ev_payload.emplace("hits", utils::Json(std::move(hits)));
    emit(active, "evidence", utils::Json(std::move(ev_payload)));
    try {
        persist(active);
    } catch (...) {
    }
}

void ResearchOrchestrator::lock_requirements(ActiveRun& active, const std::string& clarified_need,
                                             const std::string& summary) {
    if (!clarified_need.empty()) {
        active.clarified_query = clarified_need;
    }
    auto clean = humanize_user_reply(summary.empty() ? active.clarified_query : summary);
    if (clean.empty()) {
        clean = humanize_user_reply(active.run.query);
    }
    // Keep locked need clean — never leave "query | focus: …" in search keywords.
    active.clarified_query = clean;
    active.run.summary = clean;
    active.run.status = RunStatus::Running;
    active.stage_research = true;

    emit(active, "requirements_locked",
         utils::Json(utils::Json::Object{
             {"clarified_need", clean},
             {"summary", clean},
             {"stage", std::string("requirements")},
         }));

    emit(active, "next_step",
         utils::Json(utils::Json::Object{
             {"stage", std::string("research")},
             {"status", std::string("running")},
             {"note", std::string("entering deep research")},
         }));

    try {
        persist(active);
    } catch (...) {
    }

    run_deep_research(active);
}

bool ResearchOrchestrator::is_vague_ask_prompt(const std::string& prompt) {
    auto lower = prompt;
    for (auto& c : lower) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    const char* needles[] = {
        "一句话", "描述需求", "描述你的", "真正想调研", "进一步说明", "你的调研目标",
        "describe your", "what do you want", "clarify your need", "tell me what",
        "please clarify", "调研目标是什么",
    };
    for (const char* n : needles) {
        if (lower.find(n) != std::string::npos) {
            return true;
        }
    }
    return prompt.size() < 8;
}

utils::Json::Array ResearchOrchestrator::choices_from_memory(ActiveRun& active) {
    // Prefer AI-authored direction labels (never dump raw webpage titles as the choice text).
    {
        auto system = build_system_prompt(active);
        std::ostringstream user;
        user << "User original query:\n" << active.run.query << "\n\n"
             << evidence_index_text(active) << "\n"
             << "Produce JSON ONLY:\n"
             << R"JS({"prompt":"一句话说明请用户选方向","options":[{"id":"a","label":"短人类描述的调研方向","hint":"补充说明，可提及仓库/作者但不要整段粘贴网页标题"}]})JS"
             << "\nRules: 2-4 options. label/hint must be model-written research directions in the user language. "
             << "FORBIDDEN: copying raw webpage titles, SEO titles, or「威客」noise as label.\n"
             << "If the query is about a GitHub repo/author, options should be about locating that repo, README/protocol, related projects, etc.\n";
        auto raw = ask_ai_json(active, system, user.str());
        auto parsed = try_parse_json_object(raw);
        if (parsed.is_object() && parsed.contains("options") && parsed.at("options").is_array()) {
            utils::Json::Array opts;
            for (const auto& o : parsed.at("options").as_array()) {
                if (!o.is_object()) {
                    continue;
                }
                auto label = o.contains("label") ? o.at("label").as_string("") : "";
                if (label.empty() || is_vague_ask_prompt(label)) {
                    continue;
                }
                // Reject options that look like raw SEO titles.
                if (label.find("威客") != std::string::npos || label.find(" - ") != std::string::npos ||
                    label.find("_") != std::string::npos || label.size() > 60) {
                    continue;
                }
                opts.push_back(o);
                if (opts.size() >= 4) {
                    break;
                }
            }
            if (opts.size() >= 2) {
                return opts;
            }
        }
    }

    utils::Json::Array opts;
    const auto& q = active.run.query;
    const bool githubish =
        q.find("github") != std::string::npos || q.find("GitHub") != std::string::npos ||
        q.find("仓库") != std::string::npos || q.find("AboutUip") != std::string::npos ||
        q.find("XAIOP") != std::string::npos || q.find("xaiop") != std::string::npos;

    if (githubish) {
        utils::Json::Object a;
        a.emplace("id", std::string("gh_repo"));
        a.emplace("label", std::string("定位作者仓库与主页"));
        a.emplace("hint", std::string("在 GitHub 找到 AboutUip / XAIOP 仓库、星标与基础信息"));
        opts.emplace_back(std::move(a));
        utils::Json::Object b;
        b.emplace("id", std::string("gh_protocol"));
        b.emplace("label", std::string("弄清 XAIOP 是什么"));
        b.emplace("hint", std::string("协议用途、版本、SDK 入口与文档要点"));
        opts.emplace_back(std::move(b));
        utils::Json::Object c;
        c.emplace("id", std::string("gh_related"));
        c.emplace("label", std::string("辨别同名/相近项目"));
        c.emplace("hint", std::string("排除 AIOps 等相近名称，确认是否为目标项目"));
        opts.emplace_back(std::move(c));
        utils::Json::Object d;
        d.emplace("id", std::string("gh_usage"));
        d.emplace("label", std::string("怎么接入/使用"));
        d.emplace("hint", std::string("客户端或 SDK 集成方式与示例"));
        opts.emplace_back(std::move(d));
        return opts;
    }

    utils::Json::Object a;
    a.emplace("id", std::string("scope_howto"));
    a.emplace("label", std::string("偏「怎么做 / 操作策略」"));
    a.emplace("hint", std::string("实操步骤、话术、流程"));
    opts.emplace_back(std::move(a));
    utils::Json::Object b;
    b.emplace("id", std::string("scope_why"));
    b.emplace("label", std::string("偏「原理 / 机制解释」"));
    b.emplace("hint", std::string("为什么这样、底层逻辑"));
    opts.emplace_back(std::move(b));
    utils::Json::Object c;
    c.emplace("id", std::string("scope_compare"));
    c.emplace("label", std::string("偏「对比 / 选型」"));
    c.emplace("hint", std::string("方案对比、优劣、适用场景"));
    opts.emplace_back(std::move(c));
    utils::Json::Object d;
    d.emplace("id", std::string("scope_latest"));
    d.emplace("label", std::string("偏「最新动态 / 变化」"));
    d.emplace("hint", std::string("近期变化、限制、社区讨论"));
    opts.emplace_back(std::move(d));
    return opts;
}

bool ResearchOrchestrator::present_discovery_choices(ActiveRun& active, std::string prompt,
                                                     utils::Json::Array options,
                                                     const std::string& thinking) {
    // Prefer caller-provided options; otherwise synthesize direction labels.
    utils::Json::Array opts = std::move(options);
    if (opts.empty()) {
        opts = choices_from_memory(active);
    }
    if (prompt.empty() || is_vague_ask_prompt(prompt)) {
        prompt = std::string("请选择最接近你意图的调研切入点：");
    }

    active.discovery_asks += 1;
    active.searches_since_ask = 0;
    active.run.status = RunStatus::WaitingUser;
    active.run.waiting_prompt = prompt;

    utils::Json::Object ask;
    ask.emplace("type", std::string("choice"));
    ask.emplace("prompt", prompt);
    ask.emplace("options", utils::Json(std::move(opts)));
    ask.emplace("stage", active.stage_research ? std::string("research") : std::string("requirements"));
    if (!thinking.empty()) {
        ask.emplace("thinking", thinking);
    }
    emit(active, "clarify", utils::Json(std::move(ask)));
    try {
        persist(active);
    } catch (...) {
    }

    std::string reply;
    if (!wait_user_reply(active, &reply)) {
        active.run.status = RunStatus::Cancelled;
        emit(active, "cancelled", utils::Json(nullptr));
        try {
            persist(active);
        } catch (...) {
        }
        return false;
    }
    active.dialogue += "assistant_prompt: " + prompt + "\nuser: " + reply + "\n";
    active.run.summary = humanize_user_reply(reply);
    if (!active.stage_research) {
        active.clarified_query = active.run.query + " | focus: " + active.run.summary;
    }
    return true;
}

std::string ResearchOrchestrator::humanize_user_reply(const std::string& reply) {
    // "query | focus: 弄清 XAIOP…" / "query | user: gh_protocol: …" → focus label
    // "gh_protocol: 弄清 XAIOP 是什么 — 协议用途…" → "弄清 XAIOP 是什么"
    auto s = reply;
    auto trim = [](std::string& v) {
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) {
            v.erase(v.begin());
        }
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) {
            v.pop_back();
        }
    };
    trim(s);

    // Prefer the human focus after the last " | " marker.
    const auto pipe = s.rfind(" | ");
    if (pipe != std::string::npos && pipe + 3 < s.size()) {
        s = s.substr(pipe + 3);
        trim(s);
    }

    // Strip leading role / id markers: "focus:", "user:", "gh_protocol:"
    for (int i = 0; i < 3; ++i) {
        const auto colon = s.find(':');
        if (colon == std::string::npos || colon == 0 || colon >= 40) {
            break;
        }
        const auto key = s.substr(0, colon);
        const bool looks_like_id =
            key.find(' ') == std::string::npos && key.find(',') == std::string::npos;
        if (!looks_like_id) {
            break;
        }
        s = s.substr(colon + 1);
        trim(s);
    }

    const auto em = s.find(" — ");
    if (em != std::string::npos) {
        s = s.substr(0, em);
        trim(s);
    }
    return s.empty() ? reply : s;
}

void ResearchOrchestrator::finalize_after_user_choice(ActiveRun& active) {
    emit(active, "thinking",
         utils::Json(utils::Json::Object{
             {"text", std::string("已收到你的选择，正在按该切入点锁定需求（不再重复提问）。")},
             {"stage", std::string("requirements")},
         }));

    const auto& q = active.clarified_query;
    const bool githubish =
        q.find("github") != std::string::npos || q.find("GitHub") != std::string::npos ||
        q.find("仓库") != std::string::npos || q.find("AboutUip") != std::string::npos ||
        q.find("XAIOP") != std::string::npos || q.find("xaiop") != std::string::npos ||
        active.run.query.find("github") != std::string::npos ||
        active.run.query.find("GitHub") != std::string::npos ||
        active.run.query.find("XAIOP") != std::string::npos ||
        active.run.query.find("xaiop") != std::string::npos;

    try {
        if (githubish) {
            active.prefer_github = true;
            const auto need =
                humanize_user_reply(active.clarified_query.empty() ? active.run.query
                                                                   : active.clarified_query);
            run_search_round(active, "github", "repositories",
                             need.empty() ? active.run.query : need, "requirements");
        } else if (active.discovery_searches == 0) {
            run_search_round(active, "bocha", "web-search",
                             humanize_user_reply(active.clarified_query.empty()
                                                     ? active.run.query
                                                     : active.clarified_query),
                             "requirements");
        }
    } catch (...) {
    }

    const auto focus = active.run.summary.empty() ? humanize_user_reply(active.clarified_query)
                                                  : active.run.summary;
    lock_requirements(active, focus, focus);
}

void ResearchOrchestrator::worker_main(std::shared_ptr<ActiveRun> active) {
    constexpr int kMaxAiTurns = 10;
    constexpr int kMaxDiscoverySearches = 6;
    constexpr int kMaxAsks = 3;

    try {
        active->dialogue = std::string("user: ") + active->run.query + "\n";

        while (!active->cancel) {
            if (active->discovery_ai_turns >= kMaxAiTurns || active->discovery_asks >= kMaxAsks) {
                emit(*active, "thinking",
                     utils::Json(utils::Json::Object{
                         {"text", std::string("Discovery limit reached; locking the best-effort need.")},
                         {"stage", std::string("requirements")},
                     }));
                lock_requirements(*active, active->clarified_query,
                                  active->run.summary.empty()
                                      ? (std::string("已根据对话与检索锁定需求：") + active->clarified_query)
                                      : active->run.summary);
                return;
            }

            active->discovery_ai_turns += 1;
            auto system = build_system_prompt(*active);
            std::ostringstream user;
            user << "Stage: REQUIREMENTS DISCOVERY only (not final research).\n"
                 << "Original user input (already given — NEVER ask them to restate it):\n"
                 << active->run.query << "\n"
                 << "Working clarified_query: " << active->clarified_query << "\n"
                 << "discovery_searches=" << active->discovery_searches << "/" << kMaxDiscoverySearches
                 << " discovery_asks=" << active->discovery_asks << "/" << kMaxAsks << "\n"
                 << evidence_index_text(*active) << "\n"
                 << "Reply JSON ONLY with one of:\n"
                 << "1) Search to understand the fuzzy need (preferred early):\n"
                 << R"JS({"thinking":"...full reasoning...","action":"search","searches":[{"module_id":"bocha"|"github","endpoint":"web-search"|"repositories","q":"keyword"}]})JS"
                 << "\n2) Ask user ONLY with concrete multiple-choice / directions (REQUIRED options, >=2).\n"
                 << "   FORBIDDEN: asking the user to 'describe their need in one sentence' or restate the query.\n"
                 << "   The user already typed a fuzzy need; you must narrow it with options based on search hits.\n"
                 << R"JS({"thinking":"...","action":"ask_user","type":"choice","prompt":"基于检索，你更想深入哪条线？","options":[{"id":"a","label":"具体方向A","hint":"..."},{"id":"b","label":"具体方向B","hint":"..."}]})JS"
                 << "\n3) Confirm when the need is specific enough:\n"
                 << R"JS({"thinking":"...","action":"confirm","clarified_need":"...","summary":"..."})JS"
                 << "\nRules: search before ask when evidence is empty; after user picks an option prefer confirm or a NEW search,"
                 << " not another vague question. thinking must be complete (no truncation). Match user language.\n";

            auto raw = ask_ai_json(*active, system, user.str());
            auto parsed = try_parse_json_object(raw);

            std::string thinking;
            std::string action;
            if (parsed.is_object()) {
                thinking = parsed.contains("thinking") ? parsed.at("thinking").as_string("") : "";
                action = parsed.contains("action") ? parsed.at("action").as_string("") : "";
            }
            if (thinking.empty() && !raw.empty()) {
                thinking = raw; // full text — UI may collapse, never truncate here
            }
            if (thinking.empty()) {
                thinking = "Analyzing user need and deciding next discovery step…";
            }
            emit(*active, "thinking",
                 utils::Json(utils::Json::Object{
                     {"text", thinking},
                     {"stage", std::string("requirements")},
                     {"action", action},
                 }));

            if (!parsed.is_object() || action.empty()) {
                if (active->discovery_searches < 1) {
                    run_search_round(*active, "bocha", "web-search", active->clarified_query,
                                     "requirements");
                    active->searches_since_ask += 1;
                    continue;
                }
                if (active->discovery_asks >= kMaxAsks) {
                    lock_requirements(*active, active->clarified_query,
                                      std::string("已锁定：") + active->clarified_query);
                    return;
                }
                if (!present_discovery_choices(*active,
                                               "请选择最接近你意图的调研切入点：",
                                               choices_from_memory(*active), thinking)) {
                    return;
                }
                finalize_after_user_choice(*active);
                return;
            }

            if (action == "confirm") {
                auto need = parsed.contains("clarified_need")
                                ? parsed.at("clarified_need").as_string(active->clarified_query)
                                : active->clarified_query;
                auto summary =
                    parsed.contains("summary") ? parsed.at("summary").as_string(need) : need;
                if (active->discovery_searches == 0) {
                    emit(*active, "thinking",
                         utils::Json(utils::Json::Object{
                             {"text", std::string("Need at least one discovery search before confirm.")},
                             {"stage", std::string("requirements")},
                         }));
                    run_search_round(*active, "bocha", "web-search", need, "requirements");
                    active->searches_since_ask += 1;
                    continue;
                }
                lock_requirements(*active, need, summary);
                return;
            }

            if (action == "search") {
                if (active->discovery_searches >= kMaxDiscoverySearches) {
                    action = "ask_user";
                } else {
                    utils::Json::Array searches;
                    if (parsed.contains("searches") && parsed.at("searches").is_array()) {
                        searches = parsed.at("searches").as_array();
                    }
                    if (searches.empty()) {
                        utils::Json::Object one;
                        one.emplace("module_id", std::string("bocha"));
                        one.emplace("endpoint", std::string("web-search"));
                        one.emplace("q", active->clarified_query);
                        searches.emplace_back(std::move(one));
                    }
                    int ran = 0;
                    for (const auto& s : searches) {
                        if (active->cancel || active->discovery_searches >= kMaxDiscoverySearches) {
                            break;
                        }
                        if (!s.is_object()) {
                            continue;
                        }
                        std::string mid = s.contains("module_id") ? s.at("module_id").as_string("bocha")
                                                                  : "bocha";
                        std::string ep = s.contains("endpoint") ? s.at("endpoint").as_string("web-search")
                                                                : "web-search";
                        std::string q = s.contains("q") ? s.at("q").as_string(active->clarified_query)
                                                        : active->clarified_query;
                        if (q.empty()) {
                            q = active->clarified_query;
                        }
                        normalize_search_target(&mid, &ep);
                        utils::Json::Object plan;
                        plan.emplace("module_id", mid);
                        plan.emplace("endpoint", ep);
                        plan.emplace("q", q);
                        plan.emplace("keyword", q);
                        plan.emplace("purpose", std::string("requirements"));
                        emit(*active, "plan", utils::Json(std::move(plan)));
                        run_search_round(*active, mid, ep, q, "requirements");
                        active->searches_since_ask += 1;
                        ran += 1;
                        if (ran >= 2) {
                            break;
                        }
                    }
                    continue;
                }
            }

            if (action == "ask_user") {
                // After a user already answered, prefer search/confirm over another ask unless new evidence.
                if (active->discovery_asks > 0 && active->searches_since_ask == 0 &&
                    active->discovery_searches < kMaxDiscoverySearches) {
                    emit(*active, "thinking",
                         utils::Json(utils::Json::Object{
                             {"text", std::string("User already answered; searching to refine instead of re-asking.")},
                             {"stage", std::string("requirements")},
                         }));
                    run_search_round(*active, "bocha", "web-search", active->clarified_query,
                                     "requirements");
                    active->searches_since_ask += 1;
                    continue;
                }
                if (active->discovery_asks >= kMaxAsks) {
                    lock_requirements(*active, active->clarified_query,
                                      std::string("已锁定：") + active->clarified_query);
                    return;
                }

                std::string prompt =
                    parsed.contains("prompt") ? parsed.at("prompt").as_string("") : "";
                utils::Json::Array options;
                if (parsed.contains("options") && parsed.at("options").is_array()) {
                    options = parsed.at("options").as_array();
                }
                if (options.size() < 2 || is_vague_ask_prompt(prompt)) {
                    if (active->discovery_searches == 0) {
                        run_search_round(*active, "bocha", "web-search", active->clarified_query,
                                         "requirements");
                        active->searches_since_ask += 1;
                        continue;
                    }
                    options = choices_from_memory(*active);
                    prompt = "根据已检索到的线索，请选择最接近你意图的方向：";
                }
                if (!present_discovery_choices(*active, prompt, std::move(options), thinking)) {
                    return;
                }
                finalize_after_user_choice(*active);
                return;
            }

            // Unknown action → search or concrete choices, never vague free-text.
            if (active->discovery_searches == 0) {
                run_search_round(*active, "bocha", "web-search", active->clarified_query,
                                 "requirements");
                active->searches_since_ask += 1;
                continue;
            }
            if (active->discovery_asks >= kMaxAsks) {
                lock_requirements(*active, active->clarified_query,
                                  std::string("已锁定：") + active->clarified_query);
                return;
            }
            if (!present_discovery_choices(*active,
                                           "请选择你更想深入的调研方向：",
                                           choices_from_memory(*active), thinking)) {
                return;
            }
            finalize_after_user_choice(*active);
            return;
        }

        active->run.status = RunStatus::Cancelled;
        emit(*active, "cancelled", utils::Json(nullptr));
        persist(*active);
    } catch (const std::exception& ex) {
        try {
            active->run.last_error = ex.what();
            emit(*active, "thinking",
                 utils::Json(utils::Json::Object{
                     {"text", std::string("Discovery error, locking best-effort need: ") + ex.what()},
                     {"stage", std::string("requirements")},
                 }));
            if (active->discovery_searches == 0) {
                try {
                    run_search_round(*active, "bocha", "web-search", active->clarified_query,
                                     "requirements");
                } catch (...) {
                }
            }
            lock_requirements(*active, active->clarified_query,
                              std::string("需求确定阶段异常收尾：") + ex.what());
        } catch (...) {
            active->run.status = RunStatus::Failed;
            active->run.last_error = ex.what();
            emit(*active, "error", utils::Json(utils::Json::Object{{"error", std::string(ex.what())}}));
            try {
                persist(*active);
            } catch (...) {
            }
        }
    }
}

std::string ResearchOrchestrator::build_deep_system_prompt(ActiveRun& active) {
    std::ostringstream oss;
    oss << build_system_prompt(active) << "\n\n"
        << "## Deep research stage (phase 2)\n"
        << "Need is LOCKED. Answer it by researching — do not re-clarify from scratch.\n"
        << "DEPTH = layers along ONE direction; BREADTH = number of directions.\n"
        << "Prefer module=" << active.preferred_module << " endpoint=" << active.preferred_endpoint
        << (active.prefer_github ? " (GitHub: dig into code via github_rest/code).\n" : ".\n")
        << "Memory is a radiating tree (branches = side-paths / future follow-ups). "
           "Full memory bodies are NOT auto-injected — catalogs are mandatory every turn; "
           "you choose what to open.\n"
        << "Emit JSON ONLY each turn:\n"
        << R"JS({"thinking":"...","action":"search|github_rest|knowledge|memory_get|memory_chain|memory_add|ask_user|open_direction|deepen|synthesize",...)JS"
        << "\n"
        << "- search: {module_id,endpoint,q,direction_id}\n"
        << "- github_rest: {path,direction_id}\n"
        << "- knowledge: {valid:bool, nodes:[...], edges:[...]} only when valid=true\n"
        << "- memory_get: {id} load one memory body\n"
        << "- memory_chain: {id} load full chain for tip id\n"
        << "- memory_add: {title,summary?,body,kind?,direction_id?} append stage memory on current branch\n"
        << "- ask_user: {prompt,options:[...]}\n"
        << "- open_direction / deepen / synthesize as before\n";
    return oss.str();
}

std::string ResearchOrchestrator::fetch_mandatory_catalogs(ActiveRun& active) {
    std::ostringstream oss;
    oss << "## MANDATORY catalogs (engine-refreshed this turn — directory only)\n";
    try {
        auto db = ws_.open_project_db(active.run.project_id);
        MemoryTreeStore mem;
        KnowledgeGraphStore kg;
        mem.open(db);
        kg.open(db);
        oss << "### memory_catalog\n" << mem.catalog_json(active.run.project_id).dump() << "\n";
        oss << "### knowledge_graph_catalog\n" << kg.catalog_json(active.run.project_id).dump() << "\n";
        emit(active, "plan",
             utils::Json(utils::Json::Object{
                 {"stage", std::string("research")},
                 {"memory_catalog", mem.catalog_json(active.run.project_id)},
                 {"knowledge_catalog", kg.catalog_json(active.run.project_id)},
             }));
        mem.close();
        kg.close();
        db.close();
    } catch (const std::exception& ex) {
        oss << "(catalog refresh failed: " << ex.what() << ")\n";
    }
    switch (active.budget.kind) {
    case PrecisionKind::Quick:
        oss << "Read policy: QUICK — only current task or shallow related memory on this branch.\n";
        break;
    case PrecisionKind::Normal:
        oss << "Read policy: NORMAL — related deps and/or full chain on current branch.\n";
        break;
    case PrecisionKind::Deep:
        oss << "Read policy: DEEP — full current-chain memory + other branch directories on demand.\n";
        break;
    case PrecisionKind::Maximum:
        oss << "Read policy: MAXIMUM — no limits; push reading related memories and KG ops.\n";
        break;
    }
    return oss.str();
}

void ResearchOrchestrator::ensure_memory_branch(ActiveRun& active) {
    if (!active.memory_branch_id.empty()) {
        return;
    }
    try {
        auto db = ws_.open_project_db(active.run.project_id);
        MemoryTreeStore mem;
        mem.open(db);
        MemoryBranch b;
        b.id = make_id("mb_");
        b.project_id = active.run.project_id;
        b.title = "research:" + (active.clarified_query.empty() ? active.run.query : active.clarified_query);
        if (b.title.size() > 80) {
            b.title.resize(80);
        }
        b.stage = "research";
        b.run_id = active.run.id;
        mem.upsert_branch(b);
        active.memory_branch_id = b.id;
        append_stage_memory(active, "Deep research started",
                            active.clarified_query.empty() ? active.run.query : active.clarified_query,
                            "decision");
        mem.close();
        db.close();
    } catch (...) {
    }
}

void ResearchOrchestrator::append_stage_memory(ActiveRun& active, const std::string& title,
                                               const std::string& body, const std::string& kind,
                                               const std::string& direction_id) {
    if (active.memory_branch_id.empty() || title.empty()) {
        return;
    }
    try {
        auto db = ws_.open_project_db(active.run.project_id);
        MemoryTreeStore mem;
        mem.open(db);
        MemoryEntry e;
        e.id = make_id("mem_");
        e.project_id = active.run.project_id;
        e.branch_id = active.memory_branch_id;
        e.parent_id = active.memory_tip_id;
        e.run_id = active.run.id;
        e.title = title;
        e.body = body;
        e.summary = body.substr(0, std::min<std::size_t>(body.size(), 160));
        e.kind = kind.empty() ? "note" : kind;
        e.direction_id = direction_id;
        mem.upsert_entry(e);
        active.memory_tip_id = e.id;
        mem.close();
        db.close();
    } catch (...) {
    }
}

void ResearchOrchestrator::apply_memory_ops(ActiveRun& active, const utils::Json& payload) {
    if (!payload.is_object()) {
        return;
    }
    try {
        auto db = ws_.open_project_db(active.run.project_id);
        MemoryTreeStore mem;
        mem.open(db);
        if (payload.contains("title")) {
            MemoryEntry e;
            e.id = make_id("mem_");
            e.project_id = active.run.project_id;
            e.branch_id = payload.contains("branch_id") ? payload.at("branch_id").as_string("")
                                                        : active.memory_branch_id;
            e.parent_id = payload.contains("parent_id") ? payload.at("parent_id").as_string("")
                                                        : active.memory_tip_id;
            e.run_id = active.run.id;
            e.title = payload.at("title").as_string("");
            e.body = payload.contains("body") ? payload.at("body").as_string("") : "";
            e.summary = payload.contains("summary")
                            ? payload.at("summary").as_string("")
                            : e.body.substr(0, std::min<std::size_t>(e.body.size(), 160));
            e.kind = payload.contains("kind") ? payload.at("kind").as_string("note") : "note";
            e.direction_id =
                payload.contains("direction_id") ? payload.at("direction_id").as_string("") : "";
            if (!e.title.empty() && !e.branch_id.empty()) {
                mem.upsert_entry(e);
                active.memory_tip_id = e.id;
                emit(active, "thinking",
                     utils::Json(utils::Json::Object{
                         {"text", std::string("已写入阶段记忆: ") + e.title},
                         {"stage", std::string("research")},
                         {"memory_id", e.id},
                     }));
            }
        }
        mem.close();
        db.close();
    } catch (...) {
    }
}

std::string ResearchOrchestrator::directions_text(ActiveRun& active) {
    std::ostringstream oss;
    oss << "Directions (breadth) / depth layers:\n";
    if (active.directions.empty()) {
        oss << "(none yet)\n";
        return oss.str();
    }
    for (const auto& d : active.directions) {
        oss << "- [" << d.id << "] " << d.label << " depth=" << d.depth;
        if (active.budget.max_depth_layers >= 0) {
            oss << "/" << active.budget.max_depth_layers;
        } else {
            oss << "/unlimited";
        }
        if (d.closed) {
            oss << " CLOSED";
        }
        oss << "\n";
    }
    return oss.str();
}

std::string ResearchOrchestrator::knowledge_index_text(ActiveRun& active) {
    try {
        auto db = ws_.open_project_db(active.run.project_id);
        KnowledgeGraphStore kg;
        MemoryTreeStore mem;
        kg.open(db);
        mem.open(db);
        std::ostringstream oss;
        oss << "Knowledge catalog:\n" << kg.catalog_json(active.run.project_id).dump() << "\n";
        oss << "Memory catalog:\n" << mem.catalog_json(active.run.project_id).dump() << "\n";
        mem.close();
        kg.close();
        db.close();
        return oss.str();
    } catch (...) {
        return "Knowledge/memory catalogs:\n(unavailable)\n";
    }
}

void ResearchOrchestrator::analyze_and_route(ActiveRun& active) {
    const auto& need = active.clarified_query.empty() ? active.run.query : active.clarified_query;
    const auto& q = active.run.query;
    active.prefer_github =
        need.find("github") != std::string::npos || need.find("GitHub") != std::string::npos ||
        need.find("仓库") != std::string::npos || need.find("repo") != std::string::npos ||
        need.find("XAIOP") != std::string::npos || need.find("xaiop") != std::string::npos ||
        need.find("AboutUip") != std::string::npos || q.find("github") != std::string::npos ||
        q.find("GitHub") != std::string::npos || q.find("XAIOP") != std::string::npos;

    if (active.prefer_github) {
        active.preferred_module = "github";
        // Code Search is only ~10 req/min and Chinese free-text queries return noise.
        // Prefer repository search + REST for "find author/repo" needs.
        active.preferred_endpoint = "repositories";
    } else {
        active.preferred_module = "bocha";
        active.preferred_endpoint = "web-search";
    }

    std::ostringstream think;
    think << "已锁定需求，进入深度调研。分析：优先模块=" << active.preferred_module
          << " / " << active.preferred_endpoint
          << "。精度限制的是单方向深度层数（非广度）。";
    emit(active, "thinking",
         utils::Json(utils::Json::Object{
             {"text", think.str()},
             {"stage", std::string("research")},
         }));
    emit(active, "plan",
         utils::Json(utils::Json::Object{
             {"stage", std::string("research")},
             {"preferred_module", active.preferred_module},
             {"preferred_endpoint", active.preferred_endpoint},
             {"prefer_github", active.prefer_github},
             {"max_depth_layers", static_cast<std::int64_t>(active.budget.max_depth_layers)},
         }));

    // Seed with authenticated REST when owner/repo can be parsed — avoid burning Code Search quota.
    if (active.prefer_github) {
        try_github_direct_lookup(active);
    }
}

void ResearchOrchestrator::plan_directions(ActiveRun& active) {
    auto system = build_deep_system_prompt(active);
    std::ostringstream user;
    user << "Locked need:\n" << active.clarified_query << "\n\n"
         << evidence_index_text(active) << "\n"
         << "Propose 1-4 investigation DIRECTIONS (breadth). JSON ONLY:\n"
         << R"JS({"thinking":"...","directions":[{"id":"d1","label":"方向短名"}]})JS"
         << "\nDo not exceed soft max directions=" << active.budget.max_directions << ".\n";
    auto raw = ask_ai_json(active, system, user.str());
    auto parsed = try_parse_json_object(raw);
    if (parsed.is_object() && parsed.contains("directions") && parsed.at("directions").is_array()) {
        for (const auto& d : parsed.at("directions").as_array()) {
            if (!d.is_object()) {
                continue;
            }
            if (static_cast<int>(active.directions.size()) >= active.budget.max_directions) {
                break;
            }
            ResearchDirection dir;
            dir.id = d.contains("id") ? d.at("id").as_string("") : make_id("dir_");
            if (dir.id.empty()) {
                dir.id = make_id("dir_");
            }
            dir.label = d.contains("label") ? d.at("label").as_string("") : dir.id;
            if (dir.label.empty()) {
                continue;
            }
            active.directions.push_back(std::move(dir));
        }
        if (parsed.contains("thinking")) {
            emit(active, "thinking",
                 utils::Json(utils::Json::Object{
                     {"text", parsed.at("thinking").as_string("")},
                     {"stage", std::string("research")},
                 }));
        }
    }
    if (active.directions.empty()) {
        ResearchDirection dir;
        dir.id = make_id("dir_");
        dir.label = humanize_user_reply(active.clarified_query);
        if (dir.label.empty()) {
            dir.label = active.run.query;
        }
        active.directions.push_back(std::move(dir));
    }
    utils::Json::Array arr;
    for (const auto& d : active.directions) {
        arr.push_back(utils::Json(utils::Json::Object{
            {"id", d.id},
            {"label", d.label},
            {"depth", static_cast<std::int64_t>(d.depth)},
        }));
    }
    emit(active, "plan",
         utils::Json(utils::Json::Object{
             {"stage", std::string("research")},
             {"directions", std::move(arr)},
         }));
}

void ResearchOrchestrator::open_direction(ActiveRun& active, const std::string& label) {
    if (label.empty()) {
        return;
    }
    if (static_cast<int>(active.directions.size()) >= active.budget.max_directions) {
        return;
    }
    ResearchDirection dir;
    dir.id = make_id("dir_");
    dir.label = label;
    active.directions.push_back(std::move(dir));
}

bool ResearchOrchestrator::deepen_direction(ActiveRun& active, const std::string& direction_id) {
    for (auto& d : active.directions) {
        if (!direction_id.empty() && d.id != direction_id) {
            continue;
        }
        if (d.closed) {
            if (!direction_id.empty()) {
                return false;
            }
            continue;
        }
        if (active.budget.max_depth_layers >= 0 && d.depth >= active.budget.max_depth_layers) {
            d.closed = true;
            if (!direction_id.empty()) {
                return false;
            }
            continue;
        }
        d.depth += 1;
        emit(active, "keyword",
             utils::Json(utils::Json::Object{
                 {"keyword", d.label},
                 {"direction_id", d.id},
                 {"depth_layer", static_cast<std::int64_t>(d.depth)},
                 {"stage", std::string("research")},
             }));
        return true;
    }
    return false;
}

void ResearchOrchestrator::apply_knowledge_ops(ActiveRun& active, const utils::Json& payload) {
    if (!payload.is_object()) {
        return;
    }
    const bool valid = !payload.contains("valid") || payload.at("valid").as_bool(true);
    if (!valid) {
        emit(active, "thinking",
             utils::Json(utils::Json::Object{
                 {"text", std::string("模型判定知识无效，未写入关联图。")},
                 {"stage", std::string("research")},
             }));
        return;
    }

    try {
        auto db = ws_.open_project_db(active.run.project_id);
        KnowledgeGraphStore kg;
        kg.open(db);

        if (payload.contains("nodes") && payload.at("nodes").is_array()) {
            for (const auto& n : payload.at("nodes").as_array()) {
                if (!n.is_object()) {
                    continue;
                }
                KnowledgeNode node;
                node.id = n.contains("id") ? n.at("id").as_string("") : make_id("kn_");
                if (node.id.empty()) {
                    node.id = make_id("kn_");
                }
                node.project_id = active.run.project_id;
                node.run_id = active.run.id;
                node.title = n.contains("title") ? n.at("title").as_string("") : "";
                node.content = n.contains("content") ? n.at("content").as_string("") : "";
                node.kind = n.contains("kind") ? n.at("kind").as_string("fact") : "fact";
                node.direction_id =
                    n.contains("direction_id") ? n.at("direction_id").as_string("") : "";
                node.depth_layer =
                    n.contains("depth_layer") ? static_cast<int>(n.at("depth_layer").as_int64(0)) : 0;
                node.valid = true;
                if (node.title.empty()) {
                    continue;
                }
                kg.upsert_node(node);
                emit(active, "evidence",
                     utils::Json(utils::Json::Object{
                         {"kind", std::string("knowledge")},
                         {"evidence_id", node.id},
                         {"title", node.title},
                         {"snippet", node.content},
                         {"keyword", node.direction_id},
                         {"stage", std::string("research")},
                     }));
            }
        }
        if (payload.contains("edges") && payload.at("edges").is_array()) {
            for (const auto& e : payload.at("edges").as_array()) {
                if (!e.is_object()) {
                    continue;
                }
                KnowledgeEdge edge;
                edge.id = e.contains("id") ? e.at("id").as_string("") : make_id("ke_");
                if (edge.id.empty()) {
                    edge.id = make_id("ke_");
                }
                edge.project_id = active.run.project_id;
                edge.from_id = e.contains("from_id") ? e.at("from_id").as_string("") : "";
                edge.to_id = e.contains("to_id") ? e.at("to_id").as_string("") : "";
                edge.relation = e.contains("relation") ? e.at("relation").as_string("related") : "related";
                if (edge.from_id.empty() || edge.to_id.empty()) {
                    continue;
                }
                kg.upsert_edge(edge);
            }
        }

        emit(active, "plan",
             utils::Json(utils::Json::Object{
                 {"stage", std::string("research")},
                 {"knowledge_graph", kg.graph_json(active.run.project_id)},
             }));
        kg.close();
        db.close();
    } catch (const std::exception& ex) {
        emit(active, "thinking",
             utils::Json(utils::Json::Object{
                 {"text", std::string("知识关联图写入失败: ") + ex.what()},
                 {"stage", std::string("research")},
             }));
    }
}

void ResearchOrchestrator::run_github_rest(ActiveRun& active, const std::string& path,
                                           const std::string& purpose) {
    if (path.empty()) {
        return;
    }
    emit(active, "searching",
         utils::Json(utils::Json::Object{
             {"module_id", std::string("github")},
             {"endpoint", std::string("rest")},
             {"q", path},
             {"purpose", purpose},
             {"stage", std::string("research")},
         }));
    try {
        mcp::ToolRequest treq;
        treq.name = mcp::SearchToolService::kGithubRestGet;
        treq.arguments = utils::Json(utils::Json::Object{
            {"path", path},
            {"decode_content", true},
        });
        auto tools = ws_.search_tools(false);
        auto resp = tools.call(treq);
        EvidenceItem item;
        item.id = make_id("ev_");
        item.run_id = active.run.id;
        item.kind = "github";
        item.source_uri = std::string("https://api.github.com") + path;
        item.module_id = "github";
        fill_github_rest_evidence(path, resp.result, resp.ok, &item.title, &item.snippet);
        if (item.title.empty()) {
            item.title = path;
        }
        if (item.snippet.empty()) {
            item.snippet = resp.ok ? "GitHub REST OK" : (resp.error.empty() ? "REST failed" : resp.error);
        }
        item.body_json = resp.result.dump();
        item.round = active.run.search_rounds_done;
        item.created_at = utils::now_unix_seconds();
        active.memory.push_back(item);
        active.run.search_rounds_done += 1;
        if (resp.ok) {
            active.github_fail_streak = 0;
        } else {
            active.github_fail_streak += 1;
        }
        emit(active, "evidence",
             utils::Json(utils::Json::Object{
                 {"ok", resp.ok},
                 {"evidence_id", item.id},
                 {"title", item.title},
                 {"url", item.source_uri},
                 {"snippet", item.snippet},
                 {"module_id", std::string("github")},
                 {"keyword", path},
                 {"error", resp.ok ? std::string("") : resp.error},
                 {"stage", std::string("research")},
             }));
    } catch (const std::exception& ex) {
        active.github_fail_streak += 1;
        emit(active, "evidence",
             utils::Json(utils::Json::Object{
                 {"ok", false},
                 {"error", std::string(ex.what())},
                 {"keyword", path},
                 {"stage", std::string("research")},
             }));
    }
}

namespace {

bool icontains(const std::string& hay, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    auto h = hay;
    auto n = needle;
    std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return h.find(n) != std::string::npos;
}

std::string truncate_utf8(std::string s, std::size_t max_bytes) {
    if (s.size() <= max_bytes) {
        return s;
    }
    s.resize(max_bytes);
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) {
        s.pop_back();
    }
    return s + "…";
}

/// Build a human-readable snippet/title from github_rest_get JSON (status/headers/body).
void fill_github_rest_evidence(const std::string& path, const utils::Json& result, bool ok,
                               std::string* title, std::string* snippet) {
    if (!ok) {
        *title = path;
        *snippet = "GitHub REST failed";
        return;
    }
    const utils::Json* body = &result;
    if (result.is_object() && result.contains("body")) {
        body = &result.at("body");
    }
    if (body->is_object()) {
        const auto& b = *body;
        if (b.contains("full_name")) {
            const auto name = b.at("full_name").as_string(path);
            const auto desc = b.contains("description") ? b.at("description").as_string("") : "";
            const auto url = b.contains("html_url") ? b.at("html_url").as_string("") : "";
            const auto stars = b.contains("stargazers_count") ? b.at("stargazers_count").as_int64(0) : 0;
            *title = name;
            std::ostringstream sn;
            sn << desc;
            if (!desc.empty()) {
                sn << " · ";
            }
            sn << "★" << stars;
            if (!url.empty()) {
                sn << " · " << url;
            }
            *snippet = sn.str();
            return;
        }
        if (b.contains("login") && (path.find("/users/") != std::string::npos || path == "/user")) {
            const auto login = b.at("login").as_string("");
            const auto name = b.contains("name") ? b.at("name").as_string("") : "";
            const auto url = b.contains("html_url") ? b.at("html_url").as_string("") : "";
            const auto bio = b.contains("bio") ? b.at("bio").as_string("") : "";
            *title = login.empty() ? path : ("@" + login);
            std::ostringstream sn;
            if (!name.empty()) {
                sn << name << " · ";
            }
            sn << url;
            if (!bio.empty()) {
                sn << " · " << bio;
            }
            *snippet = sn.str();
            return;
        }
        if (b.contains("decoded_content")) {
            *title = path.find("readme") != std::string::npos ? std::string("README") : path;
            *snippet = truncate_utf8(b.at("decoded_content").as_string(""), 360);
            return;
        }
        if (b.contains("content") && b.contains("name")) {
            *title = b.at("name").as_string(path);
            *snippet = truncate_utf8(b.contains("decoded_content") ? b.at("decoded_content").as_string("")
                                                                  : b.at("content").as_string(""),
                                    240);
            return;
        }
    }
    if (body->is_array()) {
        std::ostringstream sn;
        int n = 0;
        for (const auto& item : body->as_array()) {
            if (!item.is_object() || n >= 8) {
                break;
            }
            std::string name;
            if (item.contains("full_name")) {
                name = item.at("full_name").as_string("");
            } else if (item.contains("name")) {
                name = item.at("name").as_string("");
            } else if (item.contains("path")) {
                name = item.at("path").as_string("");
            }
            if (name.empty()) {
                continue;
            }
            if (n) {
                sn << ", ";
            }
            sn << name;
            ++n;
        }
        *title = path;
        *snippet = n ? (std::string("items: ") + sn.str()) : "GitHub REST OK (empty list)";
        return;
    }
    *title = path;
    *snippet = "GitHub REST OK";
}

bool has_github_repo_evidence(const std::vector<EvidenceItem>& memory) {
    for (const auto& e : memory) {
        if (e.module_id != "github") {
            continue;
        }
        if (e.source_uri.find("api.github.com/repos/") != std::string::npos &&
            e.snippet.find("failed") == std::string::npos) {
            return true;
        }
        if (e.title.find('/') != std::string::npos && e.snippet.find("★") != std::string::npos) {
            return true;
        }
        if (e.title == "README" && !e.snippet.empty()) {
            return true;
        }
    }
    return false;
}

std::string build_github_facts_report(const std::string& need, const std::string& query,
                                      const std::vector<EvidenceItem>& memory) {
    std::ostringstream md;
    md << "# 调研结论\n\n";
    md << "**需求：** " << need << "\n\n";
    if (!query.empty() && query != need) {
        md << "**原始提问：** " << query << "\n\n";
    }

    std::string owner_line;
    std::string repo_line;
    std::string readme;
    std::vector<std::string> file_lines;
    std::vector<std::string> repo_list;

    for (const auto& e : memory) {
        if (e.module_id != "github" || e.body_json.empty()) {
            continue;
        }
        utils::Json root;
        try {
            root = utils::Json::parse(e.body_json);
        } catch (...) {
            continue;
        }
        const utils::Json* body = &root;
        if (root.is_object() && root.contains("body")) {
            body = &root.at("body");
        }
        if (body->is_object()) {
            const auto& b = *body;
            if (b.contains("login") && e.source_uri.find("/users/") != std::string::npos) {
                const auto login = b.at("login").as_string("");
                const auto name = b.contains("name") ? b.at("name").as_string("") : "";
                const auto html = b.contains("html_url") ? b.at("html_url").as_string("") : "";
                const auto bio = b.contains("bio") ? b.at("bio").as_string("") : "";
                std::ostringstream line;
                line << "- **作者：** @" << login;
                if (!name.empty()) {
                    line << "（" << name << "）";
                }
                if (!html.empty()) {
                    line << " — " << html;
                }
                if (!bio.empty()) {
                    line << "\n  - " << bio;
                }
                owner_line = line.str();
            } else if (b.contains("full_name")) {
                const auto full = b.at("full_name").as_string("");
                const auto desc = b.contains("description") ? b.at("description").as_string("") : "";
                const auto html = b.contains("html_url") ? b.at("html_url").as_string("") : "";
                const auto stars = b.contains("stargazers_count") ? b.at("stargazers_count").as_int64(0) : 0;
                const auto lang = b.contains("language") ? b.at("language").as_string("") : "";
                const auto def = b.contains("default_branch") ? b.at("default_branch").as_string("") : "";
                std::ostringstream line;
                line << "- **仓库：** [" << full << "](" << html << ")\n";
                if (!desc.empty()) {
                    line << "  - 简介：" << desc << "\n";
                }
                line << "  - Stars：" << stars;
                if (!lang.empty()) {
                    line << " · 语言：" << lang;
                }
                if (!def.empty()) {
                    line << " · 默认分支：" << def;
                }
                repo_line = line.str();
            } else if (b.contains("decoded_content")) {
                readme = truncate_utf8(b.at("decoded_content").as_string(""), 1200);
            }
        } else if (body->is_array()) {
            for (const auto& item : body->as_array()) {
                if (!item.is_object()) {
                    continue;
                }
                if (item.contains("full_name")) {
                    const auto full = item.at("full_name").as_string("");
                    const auto html = item.contains("html_url") ? item.at("html_url").as_string("") : "";
                    if (!full.empty()) {
                        repo_list.push_back("- [" + full + "](" + html + ")");
                    }
                } else if (item.contains("name") && item.contains("type")) {
                    const auto name = item.at("name").as_string("");
                    const auto type = item.at("type").as_string("");
                    if (!name.empty()) {
                        file_lines.push_back(std::string("- `") + name + "` (" + type + ")");
                    }
                }
            }
        }
    }

    md << "## GitHub 定位结果\n\n";
    if (!owner_line.empty()) {
        md << owner_line << "\n\n";
    }
    if (!repo_line.empty()) {
        md << repo_line << "\n\n";
    } else if (!repo_list.empty()) {
        md << "**作者仓库列表（部分）：**\n";
        for (std::size_t i = 0; i < repo_list.size() && i < 12; ++i) {
            md << repo_list[i] << "\n";
        }
        md << "\n";
    }
    if (!readme.empty()) {
        md << "## README 摘要\n\n```\n" << readme << "\n```\n\n";
    }
    if (!file_lines.empty()) {
        md << "## 仓库根目录\n\n";
        for (std::size_t i = 0; i < file_lines.size() && i < 20; ++i) {
            md << file_lines[i] << "\n";
        }
        md << "\n";
    }
    if (owner_line.empty() && repo_line.empty() && repo_list.empty()) {
        md << "_已调用 GitHub REST，但未能解析出作者/仓库字段。_\n";
    }
    return md.str();
}

std::string build_generic_evidence_report(const std::string& need, const std::string& query,
                                          const std::vector<EvidenceItem>& memory, int github_fail_streak) {
    std::ostringstream fb;
    fb << "# 调研结论\n\n";
    fb << "**需求：** " << need << "\n\n";
    if (github_fail_streak > 0) {
        fb << "> 备注：本轮曾出现 GitHub Search/API 失败。下方为已收集证据摘要。\n\n";
    }
    fb << "## 已收集证据\n\n";
    int n = 0;
    const bool prefer_gh = has_github_repo_evidence(memory);
    auto append_ev = [&](const EvidenceItem& e) {
        if (n >= 24) {
            return;
        }
        fb << "- **" << (e.title.empty() ? e.id : e.title) << "**";
        if (!e.source_uri.empty()) {
            fb << " — " << e.source_uri;
        }
        if (!e.snippet.empty()) {
            fb << "\n  - " << truncate_utf8(e.snippet, 220);
        }
        fb << "\n";
        ++n;
    };
    for (const auto& e : memory) {
        if (prefer_gh && e.module_id != "github") {
            continue;
        }
        append_ev(e);
    }
    if (n == 0) {
        for (const auto& e : memory) {
            append_ev(e);
        }
    }
    if (n == 0) {
        fb << "_（暂无可用证据。）_\n";
    }
    fb << "\n## 说明\n\n引擎根据证据自动整理（模型未返回完整综合）。\n";
    (void)query;
    return fb.str();
}

std::string extract_github_owner(const std::string& blob) {
    // Prefer explicit known / "作者是Xxx" / user:Xxx patterns.
    const auto author_pos = blob.find("作者是");
    if (author_pos != std::string::npos) {
        std::string rest = blob.substr(author_pos + 9); // UTF-8 "作者是" is 9 bytes
        std::string token;
        for (char c : rest) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                c == '_') {
                token.push_back(c);
            } else if (!token.empty()) {
                break;
            }
        }
        if (token.size() >= 2) {
            return token;
        }
    }
    if (icontains(blob, "AboutUip")) {
        return "AboutUip";
    }
    // user:login or github.com/login
    const auto user_key = blob.find("user:");
    if (user_key != std::string::npos) {
        std::string rest = blob.substr(user_key + 5);
        std::string token;
        for (char c : rest) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                c == '_') {
                token.push_back(c);
            } else if (!token.empty()) {
                break;
            }
        }
        if (!token.empty()) {
            return token;
        }
    }
    const auto gh = blob.find("github.com/");
    if (gh != std::string::npos) {
        std::string rest = blob.substr(gh + 11);
        std::string token;
        for (char c : rest) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                c == '_') {
                token.push_back(c);
            } else {
                break;
            }
        }
        if (!token.empty() && token != "search" && token != "orgs" && token != "settings") {
            return token;
        }
    }
    return {};
}

std::string extract_github_repo(const std::string& blob) {
    if (icontains(blob, "XAIOP")) {
        return "XAIOP";
    }
    return {};
}

} // namespace

void ResearchOrchestrator::try_github_direct_lookup(ActiveRun& active) {
    if (active.github_direct_tried) {
        return;
    }
    active.github_direct_tried = true;

    const std::string blob = active.run.query + " " + active.clarified_query;
    std::string owner = extract_github_owner(blob);
    std::string repo = extract_github_repo(blob);

    emit(active, "thinking",
         utils::Json(utils::Json::Object{
             {"text", std::string("改用 GitHub REST 直查：owner=") + (owner.empty() ? "?" : owner) +
                          " repo=" + (repo.empty() ? "?" : repo)},
             {"stage", active.stage_research ? std::string("research") : std::string("requirements")},
         }));

    if (owner.empty()) {
        emit(active, "thinking",
             utils::Json(utils::Json::Object{
                 {"text",
                  std::string("无法从需求解析 GitHub 用户名。Search API 403 时请确认已登录 GitHub / "
                              "PAT 有效；也可在追问中给出 github.com/用户/仓库 链接。")},
                 {"stage", active.stage_research ? std::string("research")
                                                 : std::string("requirements")},
             }));
        return;
    }

    run_github_rest(active, "/users/" + owner, "research");
    run_github_rest(active, "/users/" + owner + "/repos", "research");
    if (!repo.empty()) {
        run_github_rest(active, "/repos/" + owner + "/" + repo, "research");
        run_github_rest(active, "/repos/" + owner + "/" + repo + "/readme", "research");
        run_github_rest(active, "/repos/" + owner + "/" + repo + "/contents", "research");
    }
}

void ResearchOrchestrator::finalize_research(ActiveRun& active, const std::string& report) {
    active.run.status = RunStatus::Synthesizing;
    std::string md = report;
    if (md.empty() && has_github_repo_evidence(active.memory)) {
        md = build_github_facts_report(
            active.clarified_query.empty() ? active.run.query : active.clarified_query, active.run.query,
            active.memory);
    }
    if (md.empty()) {
        auto system = build_deep_system_prompt(active);
        std::ostringstream user;
        user << "Write the final Markdown research report for the locked need.\n"
             << "Need:\n" << active.clarified_query << "\n\n"
             << directions_text(active) << "\n"
             << evidence_index_text(active) << "\n"
             << knowledge_index_text(active) << "\n"
             << "JSON ONLY: {\"markdown\":\"...full report...\"}\n";
        auto raw = ask_ai_json(active, system, user.str());
        auto parsed = try_parse_json_object(raw);
        if (parsed.is_object() && parsed.contains("markdown")) {
            md = parsed.at("markdown").as_string("");
        }
        // Accept plain Markdown if the model ignored JSON wrapping.
        if (md.empty() && !raw.empty()) {
            auto t = raw;
            while (!t.empty() && (t.front() == ' ' || t.front() == '\n' || t.front() == '\r')) {
                t.erase(t.begin());
            }
            if (!t.empty() && (t.front() == '#' || t.find("## ") != std::string::npos ||
                               t.find("调研") != std::string::npos)) {
                md = raw;
            }
        }
        if (md.empty()) {
            md = has_github_repo_evidence(active.memory)
                     ? build_github_facts_report(
                           active.clarified_query.empty() ? active.run.query : active.clarified_query,
                           active.run.query, active.memory)
                     : build_generic_evidence_report(
                           active.clarified_query.empty() ? active.run.query : active.clarified_query,
                           active.run.query, active.memory, active.github_fail_streak);
        }
    }
    active.report_markdown = md;
    active.run.summary = active.clarified_query;
    active.run.status = RunStatus::Completed;

    emit(active, "synthesize",
         utils::Json(utils::Json::Object{
             {"markdown", md},
             {"summary", active.clarified_query},
             {"stage", std::string("research")},
         }));
    emit(active, "final",
         utils::Json(utils::Json::Object{
             {"stage", std::string("research")},
             {"summary", active.clarified_query},
             {"clarified_need", active.clarified_query},
             {"markdown", md},
             {"search_rounds_done", static_cast<std::int64_t>(active.run.search_rounds_done)},
             {"evidence_count", static_cast<std::int64_t>(active.memory.size())},
         }));
    try {
        persist(active);
    } catch (...) {
    }
}

void ResearchOrchestrator::run_deep_research(ActiveRun& active) {
    if (active.cancel) {
        return;
    }
    analyze_and_route(active);
    ensure_memory_branch(active);
    plan_directions(active);
    append_stage_memory(active, "Directions planned", directions_text(active), "note");

    // GitHub author/repo needs: REST seed already has facts — emit a real report now.
    // Avoid AI thrash loops that spam the UI and still produce empty synthesis.
    if (active.prefer_github && has_github_repo_evidence(active.memory)) {
        emit(active, "thinking",
             utils::Json(utils::Json::Object{
                 {"text",
                  std::string("已通过 GitHub REST 定位到作者/仓库，正在生成调研报告（跳过重复搜索）。")},
                 {"stage", std::string("research")},
             }));
        finalize_research(active,
                          build_github_facts_report(
                              active.clarified_query.empty() ? active.run.query : active.clarified_query,
                              active.run.query, active.memory));
        return;
    }

    constexpr int kMaxResearchAiTurns = 24;
    constexpr int kMaxResearchAsks = 4;

    while (!active.cancel && active.research_ai_turns < kMaxResearchAiTurns) {
        {
            std::lock_guard lk(active.mu);
            if (active.cancel) {
                break;
            }
        }
        active.research_ai_turns += 1;
        active.run.status = RunStatus::Running;

        auto system = build_deep_system_prompt(active);
        const auto catalogs = fetch_mandatory_catalogs(active);
        std::ostringstream user;
        user << "Locked need:\n" << active.clarified_query << "\n\n"
             << directions_text(active) << "\n"
             << catalogs << "\n"
             << "Current memory branch=" << active.memory_branch_id
             << " tip=" << active.memory_tip_id << "\n"
             << evidence_index_text(active) << "\n"
             << "Catalogs above are mandatory. Choose next action; open memory/knowledge bodies "
                "per precision read policy. Prefer deepening before too many directions.\n";
        if (active.prefer_github && has_github_repo_evidence(active.memory)) {
            user << "GitHub REST already returned author/repo facts in the evidence index. "
                    "Prefer action=\"synthesize\" with a Markdown report citing those URLs; "
                    "do NOT repeat the same Chinese keyword search.\n";
        }
        auto raw = ask_ai_json(active, system, user.str());
        auto parsed = try_parse_json_object(raw);

        if (!parsed.is_object()) {
            // Model returned non-JSON. If GitHub REST already seeded facts, stop looping
            // "定位作者… 定位作者…" searches and synthesize from evidence.
            if (active.prefer_github && has_github_repo_evidence(active.memory)) {
                finalize_research(active, "");
                return;
            }
            if (!active.directions.empty()) {
                deepen_direction(active, active.directions.front().id);
                run_search_round(active, active.preferred_module, active.preferred_endpoint,
                                 active.clarified_query + " " + active.directions.front().label,
                                 "research");
            }
            continue;
        }

        if (parsed.contains("thinking")) {
            emit(active, "thinking",
                 utils::Json(utils::Json::Object{
                     {"text", parsed.at("thinking").as_string("")},
                     {"stage", std::string("research")},
                 }));
        }

        const auto action = parsed.contains("action") ? parsed.at("action").as_string("search") : "search";

        if (action == "synthesize") {
            finalize_research(active, parsed.contains("markdown") ? parsed.at("markdown").as_string("") : "");
            return;
        }

        if (action == "open_direction") {
            open_direction(active, parsed.contains("label") ? parsed.at("label").as_string("") : "");
            continue;
        }

        if (action == "deepen") {
            const auto did =
                parsed.contains("direction_id") ? parsed.at("direction_id").as_string("") : "";
            deepen_direction(active, did);
            continue;
        }

        if (action == "knowledge") {
            apply_knowledge_ops(active, parsed);
            continue;
        }

        if (action == "memory_add") {
            apply_memory_ops(active, parsed);
            continue;
        }

        if (action == "memory_get") {
            const auto id = parsed.contains("id") ? parsed.at("id").as_string("") : "";
            try {
                auto db = ws_.open_project_db(active.run.project_id);
                MemoryTreeStore mem;
                mem.open(db);
                auto entry = mem.get_entry(active.run.project_id, id);
                if (entry) {
                    emit(active, "thinking",
                         utils::Json(utils::Json::Object{
                             {"text", std::string("读取记忆: ") + entry->title + "\n" + entry->body},
                             {"stage", std::string("research")},
                             {"memory_id", entry->id},
                         }));
                    active.dialogue += "memory_get: " + entry->title + "\n" + entry->body + "\n";
                }
                mem.close();
                db.close();
            } catch (...) {
            }
            continue;
        }

        if (action == "memory_chain") {
            const auto id = parsed.contains("id") ? parsed.at("id").as_string("") : active.memory_tip_id;
            try {
                auto db = ws_.open_project_db(active.run.project_id);
                MemoryTreeStore mem;
                mem.open(db);
                auto chain = mem.chain_json(active.run.project_id, id);
                emit(active, "plan",
                     utils::Json(utils::Json::Object{
                         {"stage", std::string("research")},
                         {"memory_chain", chain},
                     }));
                active.dialogue += "memory_chain:\n" + chain.dump() + "\n";
                mem.close();
                db.close();
            } catch (...) {
            }
            continue;
        }

        if (action == "ask_user") {
            if (active.research_asks >= kMaxResearchAsks) {
                continue;
            }
            active.research_asks += 1;
            std::string prompt = parsed.contains("prompt") ? parsed.at("prompt").as_string("")
                                                           : "请确认或调整当前调研方向：";
            utils::Json::Array options;
            if (parsed.contains("options") && parsed.at("options").is_array()) {
                options = parsed.at("options").as_array();
            }
            if (options.empty()) {
                for (const auto& d : active.directions) {
                    options.push_back(utils::Json(utils::Json::Object{
                        {"id", d.id},
                        {"label", d.label},
                        {"hint", std::string("继续该方向 (depth ") + std::to_string(d.depth) + ")"},
                    }));
                }
            }
            const auto thinking = parsed.contains("thinking") ? parsed.at("thinking").as_string("") : "";
            if (!present_discovery_choices(active, prompt, std::move(options), thinking)) {
                return;
            }
            open_direction(active, humanize_user_reply(active.run.summary));
            append_stage_memory(active, "User adjustment", active.run.summary, "ask");
            active.dialogue += "research_adjust: " + active.run.summary + "\n";
            continue;
        }

        if (action == "github_rest") {
            const auto path = parsed.contains("path") ? parsed.at("path").as_string("") : "";
            const auto did =
                parsed.contains("direction_id") ? parsed.at("direction_id").as_string("") : "";
            if (!did.empty()) {
                deepen_direction(active, did);
            }
            run_github_rest(active, path, "research");
            append_stage_memory(active, "GitHub REST " + path, path, "evidence_ref", did);
            if (parsed.contains("knowledge") && parsed.at("knowledge").is_object()) {
                apply_knowledge_ops(active, parsed.at("knowledge"));
            } else if (parsed.contains("nodes")) {
                apply_knowledge_ops(active, parsed);
            }
            continue;
        }

        // default: search
        {
            std::string mid = parsed.contains("module_id") ? parsed.at("module_id").as_string("")
                                                           : active.preferred_module;
            std::string ep = parsed.contains("endpoint") ? parsed.at("endpoint").as_string("")
                                                         : active.preferred_endpoint;
            normalize_search_target(&mid, &ep);
            std::string q = parsed.contains("q") ? parsed.at("q").as_string("") : active.clarified_query;
            // Prefer structured GitHub queries when possible.
            if (mid == "github") {
                const std::string blob = active.run.query + " " + active.clarified_query + " " + q;
                const auto owner = extract_github_owner(blob);
                const auto repo = extract_github_repo(blob);
                if (!owner.empty() && !repo.empty()) {
                    q = "repo:" + owner + "/" + repo;
                    if (ep == "code") {
                        // Keep a short token so code search is useful, not Chinese free text.
                        q = repo + " repo:" + owner + "/" + repo;
                    } else {
                        q = repo + " user:" + owner;
                        ep = "repositories";
                    }
                } else if (!owner.empty()) {
                    q = "user:" + owner;
                    ep = "repositories";
                }
                // Never send pure Chinese focus labels to code search.
                if (ep == "code" && q.find("user:") == std::string::npos &&
                    q.find("repo:") == std::string::npos) {
                    ep = "repositories";
                }
            }
            // Already have REST facts — don't burn turns re-searching the locked Chinese label.
            if (active.prefer_github && has_github_repo_evidence(active.memory) &&
                (q == active.clarified_query || q.find(active.clarified_query) != std::string::npos ||
                 active.research_ai_turns >= 3)) {
                finalize_research(active, "");
                return;
            }
            const auto did =
                parsed.contains("direction_id") ? parsed.at("direction_id").as_string("") : "";
            if (!did.empty()) {
                deepen_direction(active, did);
            } else if (!active.directions.empty()) {
                deepen_direction(active, active.directions.front().id);
            }
            try {
                run_search_round(active, mid, ep, q, "research");
                append_stage_memory(active, "Search " + mid + "/" + ep, q, "evidence_ref", did);
            } catch (...) {
            }
            if (parsed.contains("nodes") ||
                (parsed.contains("knowledge") && parsed.at("knowledge").is_object())) {
                apply_knowledge_ops(active, parsed.contains("knowledge") ? parsed.at("knowledge") : parsed);
            }
        }

        bool any_open = false;
        for (const auto& d : active.directions) {
            if (!d.closed &&
                (active.budget.max_depth_layers < 0 || d.depth < active.budget.max_depth_layers)) {
                any_open = true;
                break;
            }
        }
        if (!any_open && active.budget.max_depth_layers >= 0 && !active.directions.empty()) {
            finalize_research(active, "");
            return;
        }
    }

    if (!active.cancel) {
        finalize_research(active, "");
    }
}

} // namespace xscope::research
