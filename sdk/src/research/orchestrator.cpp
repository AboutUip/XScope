#include "xscope/research/orchestrator.hpp"

#include "xscope/ai/types.hpp"
#include "xscope/mcp/search_tools.hpp"
#include "xscope/mcp/tool_types.hpp"
#include "xscope/network/cancel.hpp"
#include "xscope/prompts/prompt_engine.hpp"
#include "xscope/providers/twtapi/client.hpp"
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
#include <unordered_set>

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

/// Extract a JSON string field value from a possibly incomplete stream buffer.
std::string extract_partial_json_string_field(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto key_pos = text.find(needle);
    if (key_pos == std::string::npos) {
        return {};
    }
    auto colon = text.find(':', key_pos + needle.size());
    if (colon == std::string::npos) {
        return {};
    }
    auto q = text.find('"', colon + 1);
    if (q == std::string::npos) {
        return {};
    }
    std::string out;
    out.reserve(256);
    for (std::size_t i = q + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\\' && i + 1 < text.size()) {
            const char n = text[++i];
            switch (n) {
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case '"':
            case '\\':
            case '/':
                out.push_back(n);
                break;
            case 'u':
                // Skip \uXXXX without decoding — keep stream responsive.
                if (i + 4 < text.size()) {
                    i += 4;
                }
                break;
            default:
                out.push_back(n);
                break;
            }
            continue;
        }
        if (c == '"') {
            break;
        }
        out.push_back(c);
    }
    return out;
}

std::string stream_display_text(const std::string& assistant) {
    // Prefer the JSON "thinking" field even when the model echoes prompt text first.
    const auto brace = assistant.find('{');
    const std::string& jsonish =
        brace != std::string::npos ? assistant.substr(brace) : assistant;
    auto thinking = extract_partial_json_string_field(jsonish, "thinking");
    if (!thinking.empty()) {
        return thinking;
    }

    // Never dump instruction echoes or raw JSON into the thinking bubble.
    if (brace != std::string::npos) {
        const auto head = assistant.substr(0, brace);
        const bool looks_instruction =
            head.find("JSON") != std::string::npos || head.find("json") != std::string::npos ||
            head.find("thinking") != std::string::npos || head.find("markdown") != std::string::npos ||
            head.find("Return") != std::string::npos || head.find("返回") != std::string::npos;
        const bool looks_payload = jsonish.find("\"thinking\"") != std::string::npos ||
                                   jsonish.find("\"markdown\"") != std::string::npos ||
                                   jsonish.find("\"action\"") != std::string::npos;
        if (looks_instruction || looks_payload) {
            return {};
        }
    }

    // Plain prose (no JSON object yet).
    if (brace == std::string::npos) {
        return assistant;
    }
    return {};
}

std::string normalize_search_key(std::string mid, std::string ep, std::string q) {
    auto lower = [](std::string& s) {
        for (auto& c : s) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
    };
    lower(mid);
    lower(ep);
    // Collapse whitespace for duplicate detection.
    std::string out;
    out.reserve(q.size());
    bool was_space = true;
    for (unsigned char c : q) {
        if (std::isspace(c)) {
            if (!was_space) {
                out.push_back(' ');
                was_space = true;
            }
            continue;
        }
        was_space = false;
        if (c >= 'A' && c <= 'Z') {
            out.push_back(static_cast<char>(c - 'A' + 'a'));
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return mid + "|" + ep + "|" + out;
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
    std::string last_live_phase;
    std::int64_t live_turn_seq = 0;
    std::int64_t last_live_turn_id = 0;
    /// Deduplicate identical module/endpoint/query triples (prevents spinning on one search).
    std::unordered_set<std::string> seen_searches;
    int stagnant_turns = 0;
    /// True when this project already has memory and/or knowledge from earlier runs (follow-up).
    bool has_project_context = false;
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
    active->has_project_context = project_has_prior_context(project_id);

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
                               {"message", active->has_project_context
                                               ? std::string("follow-up research started")
                                               : std::string("requirements discovery started")},
                               {"stage", std::string("requirements")},
                               {"follow_up", active->has_project_context},
                               {"budget_max_depth_layers", static_cast<std::int64_t>(active->budget.max_depth_layers)},
                               {"budget_max_directions", static_cast<std::int64_t>(active->budget.max_directions)},
                               {"items_per_layer", static_cast<std::int64_t>(active->budget.items_per_layer)},
                               {"ignore_cost", active->budget.ignore_cost},
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
        active.last_live_phase.clear();
        active.last_live_turn_id = 0;
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

void ResearchOrchestrator::emit_live(ActiveRun& active, const std::string& phase,
                                     const utils::Json& payload) {
    active.run.updated_at = utils::now_unix_seconds();
    std::int64_t turn_id = 0;
    if (payload.is_object() && payload.contains("turn_id")) {
        turn_id = payload.at("turn_id").as_int64(0);
    }
    auto doc = make_phase_doc(active.run, phase, payload);
    auto wire = xaiop::Bridge::instance().encode_json(doc.dump(0));
    {
        std::lock_guard lk(active.mu);
        // Coalesce rapid deltas only within the SAME AI turn. Never merge turn N into turn N+1 —
        // that made completed thinking vanish the moment the next call started.
        const bool coalesce =
            (phase == "thinking" || phase == "synthesize") && active.last_live_phase == phase &&
            turn_id != 0 && active.last_live_turn_id == turn_id && !active.phases.empty();
        if (coalesce) {
            active.phases.back() = std::move(wire);
        } else {
            while (active.phases.size() > 64) {
                active.phases.pop_front();
            }
            active.phases.push_back(std::move(wire));
        }
        active.last_live_phase = phase;
        active.last_live_turn_id = turn_id;
    }
    active.cv.notify_all();
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
    // evidence_index is no longer injected into the system template — model pulls memory/knowledge via MCP.
    ctx.extras["evidence_index"] = "";
    try {
        return ws_.prompts().render("research_system", ctx, true);
    } catch (...) {
        // Fallback if template missing.
        std::ostringstream oss;
        oss << "You are XScope research orchestrator assistant.\n"
            << "Role: clarify the need, then research it. "
               "Do not expect prior reports to be pasted — use MCP memory/knowledge tools.\n\n"
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
    oss << "Dialogue / loaded prior context:\n"
        << (active.dialogue.empty() ? "(none)\n" : active.dialogue) << "\n";
    oss << "This-run search hits only (" << active.memory.size()
        << ") — NOT project history (use catalogs / dialogue above for prior work):\n";
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
        oss << "(none yet this run — empty here does NOT mean the project has no prior data)\n";
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
        emit(active, "thinking",
             utils::Json(utils::Json::Object{
                 {"text", std::string("未选择模型，无法调用 AI。请在设置中配置供应商与模型。")},
                 {"stage", active.stage_research ? std::string("research")
                                                 : std::string("requirements")},
             }));
        return {};
    }
    if (active.cancel) {
        return {};
    }

    const auto stage = active.stage_research ? std::string("research") : std::string("requirements");
    try {
        auto ai = ws_.ai_runtime();
        ai::ChatRequest req;
        req.model_id = active.run.model_id;
        req.stream = true;
        req.stream_id = active.run.id;
        req.temperature = 1.0; // some models (e.g. DeepSeek reasoner) only allow temperature=1
        req.messages.push_back(ai::ChatMessage{"system", system, "", ""});
        req.messages.push_back(ai::ChatMessage{"user", user, "", ""});

        network::CancelToken cancel_token;
        std::string content;
        std::string reasoning;
        std::string last_error;
        std::size_t last_emit_len = 0;
        auto last_emit_at = std::chrono::steady_clock::now();

        active.live_turn_seq += 1;
        const std::int64_t turn_id = active.live_turn_seq;

        auto push_stream_ui = [&](const std::string& content_raw, const std::string& reasoning_raw,
                                  bool force) {
            const auto from_json = stream_display_text(content_raw);
            std::string display = !from_json.empty() ? from_json : reasoning_raw;
            if (display.empty()) {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - last_emit_at).count();
            if (!force && display.size() < last_emit_len + 12 && elapsed < 40) {
                return;
            }
            last_emit_len = display.size();
            last_emit_at = now;

            utils::Json::Object payload;
            payload.emplace("text", display);
            payload.emplace("stage", stage);
            payload.emplace("streaming", !force);
            payload.emplace("turn_id", turn_id);
            emit_live(active, "thinking", utils::Json(std::move(payload)));

            // Stream report body into the viewer as the markdown field grows.
            auto md = extract_partial_json_string_field(content_raw, "markdown");
            if (md.size() >= 24 || (force && !md.empty())) {
                emit_live(active, "synthesize",
                          utils::Json(utils::Json::Object{
                              {"markdown", md},
                              {"summary", active.clarified_query},
                              {"stage", stage},
                              {"streaming", !force},
                              {"turn_id", turn_id},
                          }));
            }
        };

        auto wire = ai.chat_stream_xaiop(
            req,
            [&](const std::string& phase_wire, bool is_final) {
                if (active.cancel) {
                    cancel_token.cancel();
                }
                try {
                    auto json_text = xaiop::Bridge::instance().parse_to_json(phase_wire);
                    auto doc = utils::Json::parse(json_text);
                    if (doc.contains("error") && !doc.at("error").as_string("").empty()) {
                        last_error = doc.at("error").as_string("AI error");
                    }
                    if (doc.contains("assistant") && doc.at("assistant").is_object()) {
                        const auto& asst = doc.at("assistant");
                        content = asst.contains("content") ? asst.at("content").as_string("") : "";
                        reasoning =
                            asst.contains("reasoning") ? asst.at("reasoning").as_string("") : "";
                        push_stream_ui(content, reasoning, is_final);
                    }
                } catch (...) {
                    // Ignore malformed mid-stream frames.
                }
            },
            &cancel_token);

        if (active.cancel) {
            return {};
        }

        // Prefer content for JSON actions; fall back to final wire / reasoning-only models.
        if (content.empty() && !wire.empty()) {
            try {
                auto json_text = xaiop::Bridge::instance().parse_to_json(wire);
                auto doc = utils::Json::parse(json_text);
                if (doc.contains("error") && !doc.at("error").as_string("").empty()) {
                    last_error = doc.at("error").as_string("AI error");
                }
                if (doc.contains("assistant") && doc.at("assistant").is_object()) {
                    const auto& asst = doc.at("assistant");
                    content = asst.contains("content") ? asst.at("content").as_string("") : "";
                    if (reasoning.empty() && asst.contains("reasoning")) {
                        reasoning = asst.at("reasoning").as_string("");
                    }
                }
            } catch (...) {
            }
        }

        // Some reasoners put the JSON answer only in reasoning — last resort for parse.
        const std::string& for_parse = !content.empty() ? content : reasoning;

        if (!last_error.empty() && for_parse.empty()) {
            emit(active, "thinking",
                 utils::Json(utils::Json::Object{
                     {"text", std::string("AI 返回错误: ") + last_error},
                     {"stage", stage},
                 }));
            return {};
        }
        if (for_parse.empty()) {
            emit(active, "thinking",
                 utils::Json(utils::Json::Object{
                     {"text", std::string("AI 返回了空内容（请检查模型/密钥是否可用）。")},
                     {"stage", stage},
                 }));
            return {};
        }

        const auto display_final = stream_display_text(for_parse);
        emit(active, "thinking",
             utils::Json(utils::Json::Object{
                 {"text", !display_final.empty() ? display_final
                          : (!reasoning.empty() ? reasoning : for_parse)},
                 {"stage", stage},
                 {"streaming", false},
                 {"turn_id", turn_id},
             }));
        return for_parse;
    } catch (const std::exception& ex) {
        emit(active, "thinking",
             utils::Json(utils::Json::Object{
                 {"text", std::string("AI 调用失败: ") + ex.what()},
                 {"stage", stage},
             }));
    } catch (...) {
        emit(active, "thinking",
             utils::Json(utils::Json::Object{
                 {"text", std::string("AI 调用失败: 未知错误")},
                 {"stage", stage},
             }));
    }
    return {};
}

void ResearchOrchestrator::normalize_search_target(std::string* module_id, std::string* endpoint) {
    if (!module_id || !endpoint) {
        return;
    }
    if (*module_id == "twitter" || *module_id == "x" || *module_id == "twt") {
        *module_id = "twtapi";
    }
    if (*module_id != "github" && *module_id != "bocha" && *module_id != "twtapi") {
        *module_id = "bocha";
    }
    if (*module_id == "github") {
        if (*endpoint != "repositories" && *endpoint != "code" && *endpoint != "issues" &&
            *endpoint != "commits" && *endpoint != "users" && *endpoint != "topics" &&
            *endpoint != "labels") {
            *endpoint = "repositories";
        }
    } else if (*module_id == "twtapi") {
        *endpoint = providers::twtapi::Client::normalize_endpoint(*endpoint);
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
                                            const std::string& purpose,
                                            const utils::Json* extra_args) {
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

    // Single searching phase only — do not also emit keyword (client would double-render).
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
    if (mid == "twtapi") {
        if (ep == "Search") {
            args.emplace("type", std::string("Latest"));
        }
        if (extra_args && extra_args->is_object()) {
            for (const char* key :
                 {"type", "username", "screen_name", "user_id", "tweet_id", "tweet_ids", "list_id",
                  "community_id", "cursor", "woeid", "language", "stringify_ids", "safe_search",
                  "time_filter"}) {
                if (extra_args->contains(key) && !args.contains(key)) {
                    args.emplace(key, (*extra_args).at(key));
                }
            }
        }
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
                // TwtAPI normalized tweets (Search / timelines / detail helpers).
                if (mid == "twtapi" && body.contains("_normalized") &&
                    body.at("_normalized").is_object()) {
                    const auto& norm = body.at("_normalized");
                    if (norm.contains("tweets") && norm.at("tweets").is_array()) {
                        for (const auto& t : norm.at("tweets").as_array()) {
                            if (!t.is_object()) {
                                continue;
                            }
                            if (static_cast<int>(hits.size()) >= count) {
                                break;
                            }
                            std::string rest_id =
                                t.contains("rest_id") ? t.at("rest_id").as_string("") : "";
                            std::string text;
                            if (t.contains("result") && t.at("result").is_object()) {
                                const auto& result = t.at("result");
                                if (result.contains("legacy") && result.at("legacy").is_object()) {
                                    const auto& legacy = result.at("legacy");
                                    text = legacy.contains("full_text")
                                               ? legacy.at("full_text").as_string("")
                                               : legacy.contains("text") ? legacy.at("text").as_string("")
                                                                         : "";
                                }
                            }
                            if (text.empty() && t.contains("text")) {
                                text = t.at("text").as_string("");
                            }
                            if (rest_id.empty() && text.empty()) {
                                continue;
                            }
                            EvidenceItem e;
                            e.id = make_id("ev_");
                            e.run_id = active.run.id;
                            e.kind = "tweet";
                            e.module_id = mid;
                            e.title = text.empty() ? ("tweet " + rest_id)
                                                   : (text.size() > 80 ? text.substr(0, 80) + "…" : text);
                            e.source_uri =
                                rest_id.empty() ? "" : ("https://x.com/i/status/" + rest_id);
                            e.snippet = text;
                            e.body_json = t.dump(0);
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

bool ResearchOrchestrator::looks_like_github_need(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    auto lower = text;
    for (auto& c : lower) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return lower.find("github") != std::string::npos || text.find("仓库") != std::string::npos ||
           lower.find("repo") != std::string::npos || lower.find("readme") != std::string::npos ||
           text.find("开源项目") != std::string::npos;
}

bool ResearchOrchestrator::looks_like_twitter_need(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    auto lower = text;
    for (auto& ch : lower) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    const bool zh =
        text.find("\xE6\x8E\xA8\xE7\x89\xB9") != std::string::npos || // 推特
        text.find("\xE6\x8E\xA8\xE6\x96\x87") != std::string::npos || // 推文
        text.find("\xE8\xAF\x9D\xE9\xA2\x98") != std::string::npos;   // 话题
    return lower.find("twitter") != std::string::npos || lower.find("tweet") != std::string::npos ||
           lower.find("twtapi") != std::string::npos || lower.find("x.com") != std::string::npos ||
           lower.find("hashtag") != std::string::npos || zh;
}
utils::Json::Array ResearchOrchestrator::sanitize_choice_options(const utils::Json::Array& options) {
    utils::Json::Array opts;
    int idx = 0;
    for (const auto& o : options) {
        if (!o.is_object()) {
            continue;
        }
        auto label = o.contains("label") ? o.at("label").as_string("") : "";
        auto hint = o.contains("hint") ? o.at("hint").as_string("") : "";
        auto id = o.contains("id") ? o.at("id").as_string("") : "";
        while (!label.empty() && (label.front() == ' ' || label.front() == '\t' || label.front() == '\n')) {
            label.erase(label.begin());
        }
        while (!label.empty() && (label.back() == ' ' || label.back() == '\t' || label.back() == '\n')) {
            label.pop_back();
        }
        if (label.empty()) {
            continue;
        }
        // Drop obvious SEO spam only — never rewrite meaning.
        if (label.find("威客") != std::string::npos || hint.find("威客") != std::string::npos) {
            continue;
        }
        if (label.size() > 120) {
            label = truncate_utf8(label, 120);
        }
        if (hint.size() > 200) {
            hint = truncate_utf8(hint, 200);
        }
        if (id.empty()) {
            id = std::string("opt_") + static_cast<char>('a' + (idx % 26));
        }
        utils::Json::Object clean;
        clean.emplace("id", std::move(id));
        clean.emplace("label", std::move(label));
        if (!hint.empty()) {
            clean.emplace("hint", std::move(hint));
        }
        opts.emplace_back(std::move(clean));
        ++idx;
        if (opts.size() >= 8) {
            break;
        }
    }
    return opts;
}

bool ResearchOrchestrator::present_discovery_choices(ActiveRun& active, std::string prompt,
                                                     utils::Json::Array options,
                                                     const std::string& thinking) {
    // Trust the model: ask_user means we must ask. Do not invent/replace options.
    utils::Json::Array opts = sanitize_choice_options(options);
    if (prompt.empty()) {
        prompt = std::string("请选择：");
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

bool ResearchOrchestrator::present_need_confirmation(ActiveRun& active, const std::string& need,
                                                     const std::string& summary,
                                                     const std::string& thinking) {
    auto clean = humanize_user_reply(summary.empty() ? need : summary);
    if (clean.empty()) {
        clean = humanize_user_reply(need.empty() ? active.clarified_query : need);
    }
    if (clean.empty()) {
        clean = humanize_user_reply(active.run.query);
    }

    active.run.status = RunStatus::WaitingUser;
    active.run.waiting_prompt = clean;
    active.clarified_query = clean;
    active.run.summary = clean;

    utils::Json::Object payload;
    payload.emplace("clarified_need", clean);
    payload.emplace("summary", clean);
    payload.emplace("prompt", std::string("请确认以下调研需求是否正确："));
    payload.emplace("stage", std::string("requirements"));
    if (!thinking.empty()) {
        payload.emplace("thinking", thinking);
    }
    emit(active, "confirm_need", utils::Json(std::move(payload)));
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

    auto trimmed = reply;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
        trimmed.erase(trimmed.begin());
    }
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) {
        trimmed.pop_back();
    }

    // Explicit confirm token from client, or common affirmatives.
    auto lower = trimmed;
    for (auto& c : lower) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    const bool confirmed =
        lower == "__confirm__" || lower == "confirm" || lower == "ok" || lower == "yes" ||
        trimmed == "确定" || trimmed == "确认" || trimmed == "是" || trimmed == "好" ||
        trimmed == "可以" || trimmed == "没问题";

    active.dialogue += "assistant_confirm_need: " + clean + "\nuser: " + reply + "\n";
    if (!confirmed) {
        // User wants to adjust — keep discovery open with their feedback.
        if (!trimmed.empty() && trimmed != "__reject__") {
            active.run.summary = humanize_user_reply(trimmed);
            active.clarified_query = active.run.query + " | focus: " + active.run.summary;
        }
        emit(active, "thinking",
             utils::Json(utils::Json::Object{
                 {"text", std::string("用户未确认，继续根据反馈细化需求（不臆测）。")},
                 {"stage", std::string("requirements")},
             }));
        return false;
    }
    return true;
}

std::string ResearchOrchestrator::humanize_user_reply(const std::string& reply) {
    // "query | focus: 弄清 XAIOP…" / "query | user: gh_protocol: …" → focus label
    // "gh_protocol: 弄清 XAIOP 是什么 — 协议用途…" → "弄清 XAIOP 是什么"
    auto s = reply;
    auto trim = [](std::string& v) {
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t' || v.front() == '\n')) {
            v.erase(v.begin());
        }
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\n')) {
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

    // Strip engine/UI lock prefixes that must never become search keywords.
    const char* prefixes[] = {
        "已根据对话与检索锁定需求：", "已根据对话与检索锁定需求:",
        "需求确定阶段异常收尾：", "需求确定阶段异常收尾:",
        "需求已锁定：", "需求已锁定:", "已锁定：", "已锁定:", "Locked: ", "Locked:",
        "focus:", "Focus:", "user:", "User:",
    };
    for (int pass = 0; pass < 4; ++pass) {
        bool hit = false;
        for (const char* p : prefixes) {
            const auto n = std::char_traits<char>::length(p);
            if (n > 0 && s.size() >= n && s.compare(0, n, p) == 0) {
                s = s.substr(n);
                trim(s);
                hit = true;
                break;
            }
        }
        if (!hit) {
            break;
        }
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

    // Collapse accidental exact doubling: "abcabc" → "abc"
    if (s.size() >= 8 && (s.size() % 2) == 0) {
        const auto half = s.size() / 2;
        if (s.compare(0, half, s, half, half) == 0) {
            s.resize(half);
            trim(s);
        }
    }

    return s.empty() ? reply : s;
}

void ResearchOrchestrator::worker_main(std::shared_ptr<ActiveRun> active) {
    // Ask count is intentionally unlimited — the model decides when uncertainty requires ask_user.
    // Turn budget is only a runaway safety valve (not an ask quota).
    constexpr int kMaxAiTurns = 40;
    constexpr int kMaxDiscoverySearches = 8;

    try {
        active->dialogue = std::string("user: ") + active->run.query + "\n";

        // Follow-up on a project that already has memory/KG: skip requirements discovery.
        // Prior findings live in catalogs (+ hydrate); the new query IS the locked need.
        if (active->has_project_context) {
            emit(*active, "thinking",
                 utils::Json(utils::Json::Object{
                     {"text",
                      std::string("检测到本项目已有先前调研记忆，按追问直接进入研究（跳过需求澄清）。")},
                     {"stage", std::string("requirements")},
                 }));
            lock_requirements(*active, active->clarified_query,
                              active->run.summary.empty() ? active->run.query : active->run.summary);
            return;
        }

        while (!active->cancel) {
            if (active->discovery_ai_turns >= kMaxAiTurns) {
                emit(*active, "thinking",
                     utils::Json(utils::Json::Object{
                         {"text", std::string("Discovery turn budget reached; locking the best-effort need.")},
                         {"stage", std::string("requirements")},
                     }));
                lock_requirements(*active, active->clarified_query,
                                  active->run.summary.empty() ? active->clarified_query
                                                              : active->run.summary);
                return;
            }

            active->discovery_ai_turns += 1;
            auto system = build_system_prompt(*active);
            const auto catalogs = fetch_mandatory_catalogs(*active);
            std::ostringstream user;
            user << "Stage: REQUIREMENTS DISCOVERY (clarify the research need; not the final report).\n"
                 << "User input (already given — NEVER ask them to restate it):\n"
                 << active->run.query << "\n"
                 << "Working clarified_query: " << active->clarified_query << "\n"
                 << "discovery_searches=" << active->discovery_searches << "/" << kMaxDiscoverySearches
                 << " discovery_asks_so_far=" << active->discovery_asks
                 << " (NO ask limit — you decide)\n";
            if (active->has_project_context) {
                user << "\n## Situation: FOLLOW-UP on the same project\n"
                     << "This project already has stage memory and/or a knowledge graph from prior "
                        "research. Catalogs below are directories only (JSON may be partial — that is fine).\n"
                     << "Your job: answer THIS user input. Prefer reading prior memory/knowledge "
                        "before launching new web/GitHub searches. Do not pretend amnesia.\n";
            } else {
                user << "\n## Situation: fresh project\n"
                     << "No prior project memory/knowledge yet (or catalogs empty). Search to reduce "
                        "uncertainty, then confirm the need.\n";
            }
            user << "\n" << catalogs << "\n"
                 << "Current-run hits index (this session only):\n"
                 << evidence_index_text(*active) << "\n"
                 << "Available actions (engine executes these; pick what you need):\n"
                 << "1) memory_get / memory_chain / knowledge_get — load prior bodies by catalog id\n"
                 << R"JS({"thinking":"...","action":"memory_get","id":"mem_..."})JS" << "\n"
                 << R"JS({"thinking":"...","action":"memory_chain","id":"mem_..."})JS" << "\n"
                 << R"JS({"thinking":"...","action":"knowledge_get","id":"kn_..."})JS" << "\n"
                 << "2) search — gather new facts when catalogs are insufficient\n"
                 << "   Web: module_id=bocha endpoint=web-search\n"
                 << "   GitHub: module_id=github endpoint=repositories|code|…\n"
                 << "   Twitter/X: module_id=twtapi endpoint=Search|UserByScreenName|UserTweets|TweetDetail|Trends "
                    "(ONLY if twtapi secret_configured=true in Search modules block)\n"
                 << R"JS({"thinking":"...","action":"search","searches":[{"module_id":"bocha","endpoint":"web-search","q":"..."}]})JS"
                 << "\n"
                 << R"JS({"thinking":"...","action":"search","searches":[{"module_id":"twtapi","endpoint":"Search","q":"#AI OR from:openai","type":"Latest","count":20}]})JS"
                 << "\n"
                 << R"JS({"thinking":"...","action":"search","searches":[{"module_id":"twtapi","endpoint":"UserByScreenName","username":"openai"}]})JS"
                 << "\n3) ask_user — ONLY for necessary unknowns you must NOT guess (presented verbatim)\n"
                 << R"JS({"thinking":"...","action":"ask_user","type":"choice","prompt":"...","options":[{"id":"a","label":"...","hint":"..."},{"id":"b","label":"...","hint":"..."}]})JS"
                 << "\n4) confirm — propose a clear research need for the USER to approve\n"
                 << R"JS({"thinking":"...","action":"confirm","clarified_need":"...","summary":"..."})JS"
                 << "\nRules: You decide when catalogs are enough vs when to search. Incomplete catalog "
                    "JSON is OK — open ids you care about. thinking must be complete. Match user language.\n";

            auto raw = ask_ai_json(*active, system, user.str());
            auto parsed = try_parse_json_object(raw);

            std::string thinking;
            std::string action;
            if (parsed.is_object()) {
                thinking = parsed.contains("thinking") ? parsed.at("thinking").as_string("") : "";
                action = parsed.contains("action") ? parsed.at("action").as_string("") : "";
            }
            // Thinking already streamed via ask_ai_json — do not re-emit (that wiped the bubble).

            if (!parsed.is_object() || action.empty()) {
                // Do not invent a quiz for the user — retry via search or another model turn.
                active->stagnant_turns += 1;
                if (active->discovery_searches < 1 && !active->has_project_context) {
                    run_search_round(*active, "bocha", "web-search",
                                     active->run.query.empty() ? active->clarified_query
                                                               : active->run.query,
                                     "requirements");
                    active->searches_since_ask += 1;
                    active->stagnant_turns = 0;
                    continue;
                }
                if (active->stagnant_turns >= 3) {
                    emit(*active, "plan",
                         utils::Json(utils::Json::Object{
                             {"note", std::string("模型连续未给出有效动作，按当前理解确认需求。")},
                             {"stage", std::string("requirements")},
                         }));
                    lock_requirements(*active, active->clarified_query,
                                      active->run.summary.empty() ? active->clarified_query
                                                                  : active->run.summary);
                    return;
                }
                continue;
            }
            active->stagnant_turns = 0;

            if (handle_memory_read_action(*active, parsed, action)) {
                continue;
            }

            if (action == "memory_add" || action == "knowledge") {
                if (action == "knowledge") {
                    apply_knowledge_ops(*active, parsed);
                } else {
                    apply_memory_ops(*active, parsed);
                }
                continue;
            }

            if (action == "confirm") {
                auto need = parsed.contains("clarified_need")
                                ? parsed.at("clarified_need").as_string(active->clarified_query)
                                : active->clarified_query;
                auto summary =
                    parsed.contains("summary") ? parsed.at("summary").as_string(need) : need;
                // Fresh projects: prefer at least one discovery search. Follow-ups may confirm
                // from prior memory/knowledge without a new search.
                if (active->discovery_searches == 0 && !active->has_project_context) {
                    emit(*active, "plan",
                         utils::Json(utils::Json::Object{
                             {"note", std::string("新项目建议先检索再确认；正在补一次发现检索。")},
                             {"stage", std::string("requirements")},
                         }));
                    run_search_round(*active, "bocha", "web-search", need, "requirements");
                    active->searches_since_ask += 1;
                    continue;
                }
                // Propose need to the user — never auto-lock. User must click Confirm.
                if (!present_need_confirmation(*active, need, summary, thinking)) {
                    if (active->cancel) {
                        return;
                    }
                    continue;
                }
                lock_requirements(*active, need, summary);
                return;
            }

            if (action == "search") {
                if (active->discovery_searches >= kMaxDiscoverySearches) {
                    emit(*active, "plan",
                         utils::Json(utils::Json::Object{
                             {"note",
                              std::string("探索检索次数已用尽 — 请 ask_user 澄清必要未知，或 confirm（勿臆测）。")},
                             {"stage", std::string("requirements")},
                         }));
                    continue;
                }
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
                    const auto key = normalize_search_key(mid, ep, q);
                    if (active->seen_searches.count(key)) {
                        emit(*active, "plan",
                             utils::Json(utils::Json::Object{
                                 {"note", std::string("跳过重复检索：") + mid + "/" + ep + " · " + q},
                                 {"stage", std::string("requirements")},
                             }));
                        continue;
                    }
                    active->seen_searches.insert(key);
                    // Searching event is emitted inside run_search_round — no separate plan card.
                    run_search_round(*active, mid, ep, q, "requirements", &s);
                    active->searches_since_ask += 1;
                    ran += 1;
                    if (ran >= 2) {
                        break;
                    }
                }
                continue;
            }

            if (action == "ask_user") {
                // Model chose to ask → present verbatim and resume the loop (model decides next).
                std::string prompt =
                    parsed.contains("prompt") ? parsed.at("prompt").as_string("") : "";
                utils::Json::Array options;
                if (parsed.contains("options") && parsed.at("options").is_array()) {
                    options = parsed.at("options").as_array();
                }
                if (!present_discovery_choices(*active, prompt, std::move(options), thinking)) {
                    return;
                }
                continue;
            }

            // Unknown action → do not invent a user quiz; search once or retry.
            if (active->discovery_searches == 0) {
                run_search_round(*active, "bocha", "web-search", active->clarified_query,
                                 "requirements");
                active->searches_since_ask += 1;
                continue;
            }
            continue;
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
                              active->clarified_query.empty() ? active->run.query
                                                              : active->clarified_query);
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
        << (active.prefer_github
             ? " (GitHub: dig into code via github_rest/code).\n"
             : (active.preferred_module == "twtapi"
                    ? " (Twitter/X via twtapi Search/UserTweets/TweetDetail — see twtapi SKILL).\n"
                    : ".\n"))
        << "Memory is a radiating tree (branches = side-paths / future follow-ups). "
           "Catalogs (directory JSON) are provided each turn — incomplete JSON is fine. "
           "Open what you need via actions: memory_get / memory_chain / knowledge_get. "
           "Follow-ups on the same project MUST reuse prior findings this way before re-searching.\n"
        << "Emit JSON each turn (fields you need; engine tolerates partial objects):\n"
        << R"JS({"thinking":"...","action":"search|github_rest|knowledge|knowledge_get|memory_get|memory_chain|memory_add|ask_user|open_direction|deepen|synthesize",...)JS"
        << "\n"
        << "- search: {module_id,endpoint,q,direction_id,...}\n"
        << "  Twitter/X (module_id=twtapi): endpoint=Search|UserByScreenName|UserTweets|TweetDetail|Trends|status; "
           "pass q/type/count or username/user_id/tweet_id/woeid as needed. "
           "Only when twtapi secret_configured=true.\n"
        << "  Example: {\"action\":\"search\",\"module_id\":\"twtapi\",\"endpoint\":\"Search\",\"q\":\"from:openai\",\"type\":\"Latest\"}\n"
        << "- github_rest: {path,direction_id}\n"
        << "- knowledge: {valid:bool, nodes:[...], edges:[...]} ONLY you build the graph; "
           "engine never fabricates nodes. valid=true for every meaningful reusable finding "
           "(entities/definitions/sourced claims/APIs/relationships). "
           "Each node MUST include title, content (full evidence/body), summary (your own short "
           "synthesis — not a copy of the title), and weight in [0,1] (1=core to the locked need). "
           "Include edges with from_id + to_id + relation (aliases from/to also accepted). "
           "Do not synthesize with an empty graph when solid evidence exists. "
           "If nodes already exist but edges are empty, your NEXT action MUST be knowledge "
           "that writes edges — do not keep planning.\n"
        << "- knowledge_get: {id} load one knowledge node body\n"
        << "- memory_get: {id} load one memory body\n"
        << "- memory_chain: {id} load full chain for tip id\n"
        << "- memory_add: {title,summary?,body,kind?,direction_id?} append stage memory on current branch\n"
        << "- ask_user: {prompt,options:[...]} ONLY for necessary unknowns you must not guess; "
           "presented verbatim; no ask quota\n"
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

bool ResearchOrchestrator::project_has_prior_context(const std::string& project_id) {
    if (project_id.empty()) {
        return false;
    }
    try {
        auto db = ws_.open_project_db(project_id);
        MemoryTreeStore mem;
        KnowledgeGraphStore kg;
        mem.open(db);
        kg.open(db);
        const auto mc = mem.catalog_json(project_id);
        const auto kc = kg.catalog_json(project_id);
        mem.close();
        kg.close();
        db.close();
        const bool has_mem =
            mc.is_object() &&
            ((mc.contains("entries") && mc.at("entries").is_array() &&
              !mc.at("entries").as_array().empty()) ||
             (mc.contains("branches") && mc.at("branches").is_array() &&
              !mc.at("branches").as_array().empty()));
        const bool has_kg =
            kc.is_object() &&
            ((kc.contains("nodes") && kc.at("nodes").is_array() && !kc.at("nodes").as_array().empty()) ||
             (kc.contains("entries") && kc.at("entries").is_array() &&
              !kc.at("entries").as_array().empty()));
        return has_mem || has_kg;
    } catch (...) {
        return false;
    }
}

void ResearchOrchestrator::hydrate_prior_project_context(ActiveRun& active) {
    if (!active.has_project_context || active.run.project_id.empty()) {
        return;
    }
    try {
        auto db = ws_.open_project_db(active.run.project_id);
        MemoryTreeStore mem;
        mem.open(db);
        const auto entries = mem.list_entries(active.run.project_id);
        const MemoryEntry* best_report = nullptr;
        for (const auto& e : entries) {
            if (e.kind != "report" || e.body.empty()) {
                continue;
            }
            if (!best_report || e.updated_at >= best_report->updated_at) {
                best_report = &e;
            }
        }
        if (best_report) {
            emit(active, "thinking",
                 utils::Json(utils::Json::Object{
                     {"text", std::string("已从项目记忆加载先前报告: ") + best_report->title + " (" +
                                  best_report->id + ")"},
                     {"stage", std::string("research")},
                     {"memory_id", best_report->id},
                 }));
            active.dialogue += "prior_report id=" + best_report->id + " title=" + best_report->title +
                               "\n" + best_report->body + "\n";
        }

        // Compact prior evidence titles from the latest completed run (bodies stay in DB).
        EvidenceStore store;
        const auto files =
            utils::path_from_utf8(ws_.data_root()) / "projects" / active.run.project_id / "files";
        store.open(db, files);
        std::string prior_run_id;
        for (const auto& run : store.list_runs()) {
            if (run.id == active.run.id) {
                continue;
            }
            if (run.status == RunStatus::Completed) {
                prior_run_id = run.id;
                break;
            }
        }
        if (!prior_run_id.empty()) {
            auto items = store.list_evidence(prior_run_id);
            std::ostringstream oss;
            oss << "prior_evidence_index run=" << prior_run_id << " count=" << items.size() << ":\n";
            const int limit = std::min(static_cast<int>(items.size()), 40);
            for (int i = 0; i < limit; ++i) {
                const auto& it = items[static_cast<std::size_t>(i)];
                oss << "- [" << it.id << "] " << it.module_id << " " << it.title;
                if (!it.source_uri.empty()) {
                    oss << " | " << it.source_uri;
                }
                oss << "\n";
            }
            if (static_cast<int>(items.size()) > limit) {
                oss << "- … +" << (static_cast<int>(items.size()) - limit) << " more\n";
            }
            active.dialogue += oss.str();
            emit(active, "plan",
                 utils::Json(utils::Json::Object{
                     {"stage", std::string("research")},
                     {"note", std::string("hydrated prior evidence index: ") +
                                  std::to_string(items.size()) + " items from " + prior_run_id},
                 }));
        }
        store.close();
        mem.close();
        db.close();
    } catch (...) {
    }
}

bool ResearchOrchestrator::handle_memory_read_action(ActiveRun& active, const utils::Json& parsed,
                                                     const std::string& action) {
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
                         {"stage", active.stage_research ? std::string("research")
                                                         : std::string("requirements")},
                         {"memory_id", entry->id},
                     }));
                active.dialogue += "memory_get: " + entry->title + "\n" + entry->body + "\n";
                active.stagnant_turns = 0;
            } else {
                emit(active, "plan",
                     utils::Json(utils::Json::Object{
                         {"note", std::string("memory_get: 未找到 id=") + id},
                         {"stage", active.stage_research ? std::string("research")
                                                         : std::string("requirements")},
                     }));
                active.stagnant_turns += 1;
            }
            mem.close();
            db.close();
        } catch (...) {
            active.stagnant_turns += 1;
        }
        return true;
    }
    if (action == "memory_chain") {
        const auto id =
            parsed.contains("id") ? parsed.at("id").as_string("") : active.memory_tip_id;
        try {
            auto db = ws_.open_project_db(active.run.project_id);
            MemoryTreeStore mem;
            mem.open(db);
            auto chain = mem.chain_json(active.run.project_id, id);
            emit(active, "plan",
                 utils::Json(utils::Json::Object{
                     {"stage", active.stage_research ? std::string("research")
                                                     : std::string("requirements")},
                     {"memory_chain", chain},
                 }));
            active.dialogue += "memory_chain:\n" + chain.dump() + "\n";
            mem.close();
            db.close();
            active.stagnant_turns = 0;
        } catch (...) {
            active.stagnant_turns += 1;
        }
        return true;
    }
    if (action == "knowledge_get") {
        const auto id = parsed.contains("id") ? parsed.at("id").as_string("") : "";
        try {
            auto db = ws_.open_project_db(active.run.project_id);
            KnowledgeGraphStore kg;
            kg.open(db);
            auto node = kg.get_node(active.run.project_id, id);
            if (node) {
                emit(active, "thinking",
                     utils::Json(utils::Json::Object{
                         {"text", std::string("读取知识: ") + node->title + "\n" +
                                      (node->summary.empty() ? node->content : node->summary) +
                                      (node->content.empty() || node->summary.empty()
                                           ? ""
                                           : ("\n---\n" + node->content))},
                         {"stage", active.stage_research ? std::string("research")
                                                         : std::string("requirements")},
                         {"knowledge_id", node->id},
                     }));
                active.dialogue +=
                    "knowledge_get: " + node->title + "\nsummary: " + node->summary +
                    "\nweight: " + std::to_string(node->weight < 0 ? 0.5 : node->weight) + "\n" +
                    node->content + "\n";
                active.stagnant_turns = 0;
            } else {
                emit(active, "plan",
                     utils::Json(utils::Json::Object{
                         {"note", std::string("knowledge_get: 未找到 id=") + id},
                         {"stage", active.stage_research ? std::string("research")
                                                         : std::string("requirements")},
                     }));
                active.stagnant_turns += 1;
            }
            kg.close();
            db.close();
        } catch (...) {
            active.stagnant_turns += 1;
        }
        return true;
    }
    return false;
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
    // Prefer GitHub when the need looks repo-shaped. Do not treat unrelated brand names as a trigger.
    active.prefer_github = looks_like_github_need(need) || looks_like_github_need(q);

    if (active.prefer_github) {
        active.preferred_module = "github";
        // Code Search is only ~10 req/min and Chinese free-text queries return noise.
        // Prefer repository search + REST for "find author/repo" needs.
        active.preferred_endpoint = "repositories";
    } else if (looks_like_twitter_need(need) || looks_like_twitter_need(q)) {
        active.preferred_module = "twtapi";
        active.preferred_endpoint = "Search";
    } else {
        active.preferred_module = "bocha";
        active.preferred_endpoint = "web-search";
    }

    std::ostringstream think;
    think << "已锁定需求，进入深度调研。分析：优先模块=" << active.preferred_module
          << " / " << active.preferred_endpoint << "。";
    if (active.budget.ignore_cost) {
        think << "精度=最大：不计成本，以准确度为先，可持续深入调研。";
    } else {
        think << "精度档位约束单方向深度层（快速/普通/深度有上限；最大无上限）。";
    }
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
             {"ignore_cost", active.budget.ignore_cost},
         }));

    // Seed with authenticated REST when owner/repo can be parsed — avoid burning Code Search quota.
    if (active.prefer_github) {
        try_github_direct_lookup(active);
    }
}

void ResearchOrchestrator::plan_directions(ActiveRun& active) {
    auto system = build_deep_system_prompt(active);
    const auto catalogs = fetch_mandatory_catalogs(active);
    std::ostringstream user;
    user << "Locked need:\n" << active.clarified_query << "\n\n";
    if (active.has_project_context) {
        user << "## FOLLOW-UP — prior project data is available\n"
             << "Catalogs below list prior memory/knowledge. Dialogue may already include "
                "prior_report / prior_evidence_index. Do NOT claim the project is empty just "
                "because this-run search hits are 0. Prefer reading/using prior findings "
                "(especially report kind) before proposing blind inventory searches.\n\n";
    }
    user << catalogs << "\n"
         << evidence_index_text(active) << "\n"
         << "Propose 1-4 investigation DIRECTIONS (breadth). JSON ONLY:\n"
         << R"JS({"thinking":"...","directions":[{"id":"d1","label":"方向短名"}]})JS"
         << "\n";
    if (active.budget.max_directions < 0) {
        user << "Direction count is unlimited at Maximum — open what accuracy needs.\n";
    } else {
        user << "Do not exceed soft max directions=" << active.budget.max_directions << ".\n";
    }
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
        // Thinking already streamed via ask_ai_json.
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
    if (active.budget.max_directions >= 0 &&
        static_cast<int>(active.directions.size()) >= active.budget.max_directions) {
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

ResearchOrchestrator::KnowledgeWriteResult
ResearchOrchestrator::apply_knowledge_ops(ActiveRun& active, const utils::Json& payload) {
    KnowledgeWriteResult result;
    if (!payload.is_object()) {
        return result;
    }
    const bool valid = !payload.contains("valid") || payload.at("valid").as_bool(true);
    if (!valid) {
        emit(active, "thinking",
             utils::Json(utils::Json::Object{
                 {"text", std::string("模型判定知识无效，未写入关联图。")},
                 {"stage", std::string("research")},
             }));
        return result;
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
                node.summary = n.contains("summary") ? n.at("summary").as_string("") : "";
                if (node.summary.empty() && !node.content.empty()) {
                    node.summary =
                        node.content.substr(0, std::min<std::size_t>(node.content.size(), 240));
                }
                node.weight = n.contains("weight") ? n.at("weight").as_number(0.5) : 0.5;
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
                result.nodes_written += 1;
                emit(active, "evidence",
                     utils::Json(utils::Json::Object{
                         {"kind", std::string("knowledge")},
                         {"evidence_id", node.id},
                         {"title", node.title},
                         {"snippet", node.summary.empty() ? node.content : node.summary},
                         {"weight", node.weight < 0 ? 0.5 : node.weight},
                         {"keyword", node.direction_id},
                         {"stage", std::string("research")},
                     }));
            }
        }
        if (payload.contains("edges") && payload.at("edges").is_array()) {
            for (const auto& e : payload.at("edges").as_array()) {
                if (!e.is_object()) {
                    result.edges_skipped += 1;
                    continue;
                }
                KnowledgeEdge edge;
                edge.id = e.contains("id") ? e.at("id").as_string("") : make_id("ke_");
                if (edge.id.empty()) {
                    edge.id = make_id("ke_");
                }
                edge.project_id = active.run.project_id;
                // Accept common aliases — models often emit from/to or source/target.
                auto pick_id = [&](std::initializer_list<const char*> keys) -> std::string {
                    for (const char* k : keys) {
                        if (e.contains(k)) {
                            auto v = e.at(k).as_string("");
                            if (!v.empty()) {
                                return v;
                            }
                        }
                    }
                    return {};
                };
                edge.from_id = pick_id({"from_id", "from", "source_id", "source", "src", "src_id"});
                edge.to_id = pick_id({"to_id", "to", "target_id", "target", "dst", "dst_id"});
                edge.relation = e.contains("relation") ? e.at("relation").as_string("related")
                                : (e.contains("type") ? e.at("type").as_string("related")
                                                      : (e.contains("rel") ? e.at("rel").as_string("related")
                                                                          : "related"));
                if (edge.from_id.empty() || edge.to_id.empty()) {
                    result.edges_skipped += 1;
                    continue;
                }
                kg.upsert_edge(edge);
                result.edges_written += 1;
            }
            if (result.edges_skipped > 0 || result.edges_written > 0) {
                emit(active, "plan",
                     utils::Json(utils::Json::Object{
                         {"stage", std::string("research")},
                         {"note", std::string("knowledge edges written=") +
                                      std::to_string(result.edges_written) + " skipped=" +
                                      std::to_string(result.edges_skipped) +
                                      " (need from_id/to_id or from/to aliases)"},
                     }));
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
    return result;
}

void ResearchOrchestrator::publish_knowledge_graph_snapshot(ActiveRun& active) {
    try {
        auto db = ws_.open_project_db(active.run.project_id);
        KnowledgeGraphStore kg;
        kg.open(db);
        emit(active, "plan",
             utils::Json(utils::Json::Object{
                 {"stage", std::string("research")},
                 {"knowledge_graph", kg.graph_json(active.run.project_id)},
             }));
        kg.close();
        db.close();
    } catch (...) {
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
    fb << "\n## 说明\n\n";
    if (n == 0) {
        fb << "本轮未收集到可用证据，且模型未能生成完整综合。请检查 AI 密钥/模型，"
              "或补充更具体的 GitHub 链接后再试。\n";
    } else {
        fb << "以上为引擎根据证据自动整理的摘要（模型未返回完整综合）。\n";
    }
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
    // user:login or github.com/login[/repo]
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
    // owner/repo shorthand
    for (size_t i = 0; i + 2 < blob.size(); ++i) {
        const char c = blob[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            continue;
        }
        size_t j = i;
        while (j < blob.size()) {
            const char d = blob[j];
            if ((d >= 'A' && d <= 'Z') || (d >= 'a' && d <= 'z') || (d >= '0' && d <= '9') || d == '-' ||
                d == '_') {
                ++j;
            } else {
                break;
            }
        }
        if (j < blob.size() && blob[j] == '/' && j + 1 < blob.size()) {
            const auto owner = blob.substr(i, j - i);
            char n = blob[j + 1];
            if (((n >= 'A' && n <= 'Z') || (n >= 'a' && n <= 'z') || (n >= '0' && n <= '9')) &&
                owner.size() >= 2 && owner != "http" && owner != "https") {
                return owner;
            }
        }
        i = j;
    }
    return {};
}

std::string extract_github_repo(const std::string& blob) {
    // github.com/owner/repo
    const auto gh = blob.find("github.com/");
    if (gh != std::string::npos) {
        std::string rest = blob.substr(gh + 11);
        const auto slash = rest.find('/');
        if (slash != std::string::npos && slash + 1 < rest.size()) {
            std::string token;
            for (size_t i = slash + 1; i < rest.size(); ++i) {
                const char c = rest[i];
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '-' || c == '_' || c == '.') {
                    token.push_back(c);
                } else {
                    break;
                }
            }
            if (token.size() >= 2) {
                return token;
            }
        }
    }
    // owner/repo shorthand
    for (size_t i = 0; i + 2 < blob.size(); ++i) {
        const char c = blob[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            continue;
        }
        size_t j = i;
        while (j < blob.size()) {
            const char d = blob[j];
            if ((d >= 'A' && d <= 'Z') || (d >= 'a' && d <= 'z') || (d >= '0' && d <= '9') || d == '-' ||
                d == '_') {
                ++j;
            } else {
                break;
            }
        }
        if (j < blob.size() && blob[j] == '/' && j + 1 < blob.size()) {
            std::string token;
            for (size_t k = j + 1; k < blob.size(); ++k) {
                const char e = blob[k];
                if ((e >= 'A' && e <= 'Z') || (e >= 'a' && e <= 'z') || (e >= '0' && e <= '9') ||
                    e == '-' || e == '_' || e == '.') {
                    token.push_back(e);
                } else {
                    break;
                }
            }
            const auto owner = blob.substr(i, j - i);
            if (token.size() >= 2 && owner != "http" && owner != "https") {
                return token;
            }
        }
        i = j;
    }
    // Labels: 项目：Name / 仓库：Name / repo: Name / project Name
    const char* labels[] = {"项目：", "项目:", "仓库：", "仓库:", "repo:", "Repo:", "repository:",
                            "项目是", "仓库是"};
    for (const char* label : labels) {
        const auto pos = blob.find(label);
        if (pos == std::string::npos) {
            continue;
        }
        const auto n = std::char_traits<char>::length(label);
        std::string rest = blob.substr(pos + n);
        while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t' || rest.front() == '\"' ||
                                 rest.front() == '\'')) {
            rest.erase(rest.begin());
        }
        std::string token;
        for (char c : rest) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                c == '_' || c == '.') {
                token.push_back(c);
            } else if (!token.empty()) {
                break;
            }
        }
        if (token.size() >= 2) {
            return token;
        }
    }
    // Standalone CamelCase / kebab project token near "github" (e.g. ZerOS-System).
    if (icontains(blob, "github") || blob.find("仓库") != std::string::npos ||
        blob.find("开源") != std::string::npos) {
        std::string best;
        for (size_t i = 0; i < blob.size(); ++i) {
            const char c = blob[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
                continue;
            }
            size_t j = i;
            bool has_upper = false;
            bool has_sep = false;
            while (j < blob.size()) {
                const char d = blob[j];
                if ((d >= 'A' && d <= 'Z') || (d >= 'a' && d <= 'z') || (d >= '0' && d <= '9') ||
                    d == '-' || d == '_') {
                    if (d >= 'A' && d <= 'Z') {
                        has_upper = true;
                    }
                    if (d == '-' || d == '_') {
                        has_sep = true;
                    }
                    ++j;
                } else {
                    break;
                }
            }
            auto token = blob.substr(i, j - i);
            if (token.size() >= 4 && token.size() <= 64 && (has_upper || has_sep) &&
                !icontains(token, "github") && !icontains(token, "http")) {
                if (token.size() > best.size()) {
                    best = token;
                }
            }
            i = j;
        }
        if (!best.empty()) {
            return best;
        }
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

    if (owner.empty() && !repo.empty()) {
        // Have a project name but no owner — search repositories by name.
        emit(active, "thinking",
             utils::Json(utils::Json::Object{
                 {"text", std::string("未解析到作者，按仓库名搜索: ") + repo},
                 {"stage", active.stage_research ? std::string("research")
                                                 : std::string("requirements")},
             }));
        try {
            run_search_round(active, "github", "repositories", repo, 
                             active.stage_research ? "research" : "requirements");
        } catch (...) {
        }
        return;
    }

    if (owner.empty()) {
        emit(active, "thinking",
             utils::Json(utils::Json::Object{
                 {"text",
                  std::string("无法从需求解析 GitHub 用户名/仓库。请补充 github.com/用户/仓库 "
                              "或明确作者与项目名。")},
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
    emit(active, "thinking",
         utils::Json(utils::Json::Object{
             {"text", std::string("正在生成调研报告…")},
             {"stage", std::string("research")},
         }));

    std::string md = report;

    // Always prefer a fresh model synthesis when the caller did not supply a full report.
    // Pre-baked evidence dumps are fallback only after AI fails.
    if (md.empty() || md.find("引擎根据证据自动整理") != std::string::npos) {
        auto system = build_deep_system_prompt(active);
        std::ostringstream user;
        user << "Write the final Markdown research report for the locked need.\n"
             << "Use the user's language. Be concrete; cite evidence titles/URLs when present.\n"
             << "Need:\n" << active.clarified_query << "\n\n"
             << "Original query:\n" << active.run.query << "\n\n"
             << directions_text(active) << "\n"
             << evidence_index_text(active) << "\n"
             << knowledge_index_text(active) << "\n"
             << "Return a JSON object with keys thinking (brief) and markdown (full report). "
                "Do not repeat these instructions in the output.\n"
             << "If evidence is thin, still write a structured report of what is known and what is missing.\n";
        auto raw = ask_ai_json(active, system, user.str());
        auto parsed = try_parse_json_object(raw);
        if (parsed.is_object()) {
            // Thinking already streamed via ask_ai_json.
            if (parsed.contains("markdown")) {
                md = parsed.at("markdown").as_string("");
            }
        }
        // Accept plain Markdown if the model ignored JSON wrapping.
        if (md.empty() && !raw.empty()) {
            auto t = raw;
            while (!t.empty() && (t.front() == ' ' || t.front() == '\n' || t.front() == '\r' ||
                                  t.front() == '`')) {
                t.erase(t.begin());
            }
            if (!t.empty() && t.rfind("```", 0) == 0) {
                // strip markdown fence
                auto end = t.find("```", 3);
                if (end != std::string::npos) {
                    t = t.substr(3, end - 3);
                    if (t.rfind("markdown", 0) == 0 || t.rfind("md", 0) == 0) {
                        auto nl = t.find('\n');
                        if (nl != std::string::npos) {
                            t = t.substr(nl + 1);
                        }
                    }
                }
            }
            if (!t.empty() && (t.front() == '#' || t.find("## ") != std::string::npos ||
                               t.find('\n') != std::string::npos || t.size() > 80)) {
                md = t;
            }
        }
    }

    if (md.empty() && has_github_repo_evidence(active.memory)) {
        md = build_github_facts_report(
            active.clarified_query.empty() ? active.run.query : active.clarified_query, active.run.query,
            active.memory);
    }
    if (md.empty()) {
        emit(active, "thinking",
             utils::Json(utils::Json::Object{
                 {"text", std::string("模型未产出报告，改用已收集证据生成摘要。")},
                 {"stage", std::string("research")},
             }));
        md = build_generic_evidence_report(
            active.clarified_query.empty() ? active.run.query : active.clarified_query,
            active.run.query, active.memory, active.github_fail_streak);
    }

    active.report_markdown = md;
    active.run.summary = active.clarified_query;
    active.run.status = RunStatus::Completed;

    // Persist report into project memory so follow-ups can memory_get it from the catalog.
    try {
        ensure_memory_branch(active);
        append_stage_memory(active, "Research report", md, "report");
    } catch (...) {
    }

    
    // Publish model-authored graph only (engine never fabricates nodes).
    publish_knowledge_graph_snapshot(active);

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
    hydrate_prior_project_context(active);
    analyze_and_route(active);
    ensure_memory_branch(active);
    plan_directions(active);
    append_stage_memory(active, "Directions planned", directions_text(active), "note");

    // GitHub author/repo needs with REST facts: still ask the model to write the report.
    if (active.prefer_github && has_github_repo_evidence(active.memory)) {
        emit(active, "thinking",
             utils::Json(utils::Json::Object{
                 {"text",
                  std::string("已通过 GitHub 收集到仓库线索，正在请模型撰写调研报告…")},
                 {"stage", std::string("research")},
             }));
        finalize_research(active, "");
        return;
    }

    // Maximum: ignore cost — only a runaway safety valve (not a research budget).
    // Other tiers: modest turn budget derived from depth × breadth caps.
    const int kMaxResearchAiTurns =
        active.budget.ignore_cost
            ? 500
            : std::clamp(8 + active.budget.max_directions * 2 + active.budget.max_depth_layers, 10, 40);

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
                "per precision read policy. Prefer deepening before too many directions.\n"
             << "Knowledge graph: after solid findings, emit action=knowledge (valid=true, "
                "nodes+edges) BEFORE synthesize. Engine never auto-builds the graph. "
                "Each node needs title + content + summary (AI synthesis) + weight 0-1. "
                "Edge objects need from_id+to_id (aliases from/to also work). "
                "After a knowledge write, trust the refreshed catalog counts — if edges are "
                "still 0, your previous edges did not land; fix the payload, do not only re-plan.\n";
        if (active.has_project_context) {
            user << "FOLLOW-UP: this-run search hits may be empty while prior_report / "
                    "prior_evidence_index already sit in dialogue — use them (and catalogs) "
                    "before inventing a from-scratch inventory.\n";
        }
        if (active.budget.ignore_cost) {
            user << "MAXIMUM MODE: ignore cost/time. Keep deepening for accuracy. "
                    "Do NOT repeat a search that already ran — pick a deeper/different query "
                    "or synthesize when evidence is solid.\n";
        }
        if (!active.seen_searches.empty()) {
            user << "Already-executed searches (do not repeat these exact module|endpoint|q):\n";
            int shown = 0;
            for (const auto& key : active.seen_searches) {
                if (shown++ >= 24) {
                    user << "- …\n";
                    break;
                }
                user << "- " << key << "\n";
            }
        }
        if (active.prefer_github && has_github_repo_evidence(active.memory)) {
            user << "GitHub REST already returned author/repo facts in the evidence index. "
                    "Prefer action=\"synthesize\" with a Markdown report citing those URLs; "
                    "do NOT repeat the same Chinese keyword search.\n";
        }
        auto raw = ask_ai_json(active, system, user.str());
        auto parsed = try_parse_json_object(raw);

        if (!parsed.is_object()) {
            // Non-JSON / unparseable action. Do NOT re-run clarified_query + direction label
            // (that produced loops like "ZerOS-System ZerOS-System" while thinking looked fine).
            if (active.prefer_github && has_github_repo_evidence(active.memory)) {
                finalize_research(active, "");
                return;
            }
            active.stagnant_turns += 1;
            emit(active, "plan",
                 utils::Json(utils::Json::Object{
                     {"note", std::string("本回合未解析到可执行动作。目录/工具仍可用："
                                          "memory_get、knowledge_get、search、github_rest、synthesize。")},
                     {"stage", std::string("research")},
                 }));
            if (active.prefer_github) {
                try_github_direct_lookup(active);
            }
            // Max still needs a runaway brake for identical no-progress turns.
            if (active.stagnant_turns >= (active.budget.ignore_cost ? 6 : 3)) {
                finalize_research(active, "");
                return;
            }
            continue;
        }

        if (!parsed.contains("action") || parsed.at("action").as_string("").empty()) {
            active.stagnant_turns += 1;
            emit(active, "plan",
                 utils::Json(utils::Json::Object{
                     {"note", std::string("本回合未给出 action；请自行决定下一步（可读记忆/知识或检索）。")},
                     {"stage", std::string("research")},
                 }));
            if (active.stagnant_turns >= (active.budget.ignore_cost ? 6 : 3)) {
                finalize_research(active, "");
                return;
            }
            continue;
        }
        const auto action = parsed.at("action").as_string("");

        if (handle_memory_read_action(active, parsed, action)) {
            continue;
        }

        if (action == "synthesize") {
            finalize_research(active, parsed.contains("markdown") ? parsed.at("markdown").as_string("") : "");
            return;
        }

        if (action == "open_direction") {
            open_direction(active, parsed.contains("label") ? parsed.at("label").as_string("") : "");
            active.stagnant_turns = 0;
            continue;
        }

        if (action == "deepen") {
            const auto did =
                parsed.contains("direction_id") ? parsed.at("direction_id").as_string("") : "";
            if (!deepen_direction(active, did)) {
                active.stagnant_turns += 1;
            } else {
                active.stagnant_turns = 0;
            }
            continue;
        }

        if (action == "knowledge") {
            const auto wr = apply_knowledge_ops(active, parsed);
            active.stagnant_turns = 0;
            // Honest feedback only — model decides next step from refreshed catalogs.
            emit(active, "plan",
                 utils::Json(utils::Json::Object{
                     {"stage", std::string("research")},
                     {"note", std::string("knowledge applied: nodes=") +
                                  std::to_string(wr.nodes_written) + " edges=" +
                                  std::to_string(wr.edges_written) + " edges_skipped=" +
                                  std::to_string(wr.edges_skipped)},
                 }));
            continue;
        }

        if (action == "memory_add") {
            apply_memory_ops(active, parsed);
            active.stagnant_turns = 0;
            continue;
        }

        if (action == "ask_user") {
            // No ask quota — model asks only for necessary unknowns; present verbatim.
            active.research_asks += 1;
            std::string prompt = parsed.contains("prompt") ? parsed.at("prompt").as_string("")
                                                           : "";
            utils::Json::Array options;
            if (parsed.contains("options") && parsed.at("options").is_array()) {
                options = parsed.at("options").as_array();
            }
            const auto thinking = parsed.contains("thinking") ? parsed.at("thinking").as_string("") : "";
            if (!present_discovery_choices(active, prompt, std::move(options), thinking)) {
                return;
            }
            open_direction(active, humanize_user_reply(active.run.summary));
            append_stage_memory(active, "User adjustment", active.run.summary, "ask");
            active.dialogue += "research_adjust: " + active.run.summary + "\n";
            active.stagnant_turns = 0;
            continue;
        }

        if (action == "github_rest") {
            const auto path = parsed.contains("path") ? parsed.at("path").as_string("") : "";
            const auto did =
                parsed.contains("direction_id") ? parsed.at("direction_id").as_string("") : "";
            const auto rest_key = normalize_search_key("github", "rest", path);
            if (!path.empty() && active.seen_searches.count(rest_key)) {
                active.stagnant_turns += 1;
                emit(active, "plan",
                     utils::Json(utils::Json::Object{
                         {"note", std::string("跳过重复的 GitHub REST：") + path +
                                      "。请换更深路径，或在证据充分时 synthesize。"},
                         {"stage", std::string("research")},
                     }));
                // Stuck on repeats only: safety valve (Maximum still allows many unique digs).
                if (active.stagnant_turns >= 8) {
                    finalize_research(active, "");
                    return;
                }
                continue;
            }
            if (!path.empty()) {
                active.seen_searches.insert(rest_key);
            }
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
            active.stagnant_turns = 0;
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
                (q == active.clarified_query || q.find(active.clarified_query) != std::string::npos) &&
                !active.budget.ignore_cost) {
                finalize_research(active, "");
                return;
            }
            // At Maximum with GitHub facts: still allow deeper unique digs, but not the same label search.
            if (active.prefer_github && has_github_repo_evidence(active.memory) &&
                (q == active.clarified_query || q.find(active.clarified_query) != std::string::npos)) {
                emit(active, "plan",
                     utils::Json(utils::Json::Object{
                         {"note", std::string("已有仓库证据，跳过重复关键词检索。请 github_rest/"
                                              "code 深挖或 synthesize。")},
                         {"stage", std::string("research")},
                     }));
                active.stagnant_turns += 1;
                if (active.stagnant_turns >= 8) {
                    finalize_research(active, "");
                    return;
                }
                continue;
            }

            const auto search_key = normalize_search_key(mid, ep, q);
            if (active.seen_searches.count(search_key)) {
                active.stagnant_turns += 1;
                emit(active, "plan",
                     utils::Json(utils::Json::Object{
                         {"note", std::string("跳过重复检索：") + mid + "/" + ep + " · " + q +
                                      "。请换更深/不同的查询，或在证据充分时 synthesize。"},
                         {"stage", std::string("research")},
                     }));
                if (active.stagnant_turns >= 8 ||
                    (!active.budget.ignore_cost && active.stagnant_turns >= 3)) {
                    finalize_research(active, "");
                    return;
                }
                continue;
            }
            active.seen_searches.insert(search_key);

            const auto did =
                parsed.contains("direction_id") ? parsed.at("direction_id").as_string("") : "";
            if (!did.empty()) {
                deepen_direction(active, did);
            } else if (!active.directions.empty()) {
                deepen_direction(active, active.directions.front().id);
            }
            try {
                run_search_round(active, mid, ep, q, "research", &parsed);
                append_stage_memory(active, "Search " + mid + "/" + ep, q, "evidence_ref", did);
                active.stagnant_turns = 0;
            } catch (...) {
                active.stagnant_turns += 1;
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
        // Only auto-stop when depth is capped and every direction is exhausted.
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
