#pragma once

#include "xscope/research/evidence_store.hpp"
#include "xscope/research/knowledge_graph.hpp"
#include "xscope/research/memory_tree.hpp"
#include "xscope/research/precision.hpp"
#include "xscope/research/types.hpp"
#include "xscope/storage/workspace.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace xscope::research {

/// Process-local research run manager: start/continue/cancel + XAIOP phase queue.
class ResearchOrchestrator {
public:
    explicit ResearchOrchestrator(storage::Workspace& workspace);
    ~ResearchOrchestrator();

    ResearchOrchestrator(const ResearchOrchestrator&) = delete;
    ResearchOrchestrator& operator=(const ResearchOrchestrator&) = delete;

    std::string start(const std::string& project_id, const std::string& query,
                      const std::string& model_id, PrecisionKind precision);

    void continue_with_user(const std::string& run_id, const std::string& user_reply);

    void cancel(const std::string& run_id);

    std::string poll_xaiop(const std::string& run_id, int wait_ms);

    std::optional<ResearchRun> status(const std::string& run_id);
    std::vector<EvidenceItem> evidence_list(const std::string& project_id,
                                            const std::string& run_id);

private:
    struct ActiveRun;
    struct ResearchDirection;

    void worker_main(std::shared_ptr<ActiveRun> active);
    void emit(ActiveRun& active, const std::string& phase, const utils::Json& payload = utils::Json(nullptr));
    void persist(ActiveRun& active);
    std::string ask_ai_json(ActiveRun& active, const std::string& system, const std::string& user);
    utils::Json try_parse_json_object(const std::string& text);
    void run_search_round(ActiveRun& active, const std::string& module_id, const std::string& endpoint,
                          const std::string& q, const std::string& purpose);
    void run_github_rest(ActiveRun& active, const std::string& path, const std::string& purpose);
    /// When GitHub search 403/fails but need is GitHub-shaped, hit users/repos REST directly.
    void try_github_direct_lookup(ActiveRun& active);
    bool wait_user_reply(ActiveRun& active, std::string* out_reply);
    void lock_requirements(ActiveRun& active, const std::string& clarified_need, const std::string& summary);
    bool present_discovery_choices(ActiveRun& active, std::string prompt, utils::Json::Array options,
                                   const std::string& thinking);
    void finalize_after_user_choice(ActiveRun& active);
    utils::Json::Array choices_from_memory(ActiveRun& active);
    static bool is_vague_ask_prompt(const std::string& prompt);
    static std::string humanize_user_reply(const std::string& reply);
    std::string build_system_prompt(ActiveRun& active);
    std::string build_deep_system_prompt(ActiveRun& active);
    std::string evidence_index_text(ActiveRun& active);
    std::string directions_text(ActiveRun& active);
    std::string knowledge_index_text(ActiveRun& active);
    std::string fetch_mandatory_catalogs(ActiveRun& active);
    void ensure_memory_branch(ActiveRun& active);
    void append_stage_memory(ActiveRun& active, const std::string& title, const std::string& body,
                             const std::string& kind, const std::string& direction_id = "");
    void apply_memory_ops(ActiveRun& active, const utils::Json& payload);
    void analyze_and_route(ActiveRun& active);
    void plan_directions(ActiveRun& active);
    void run_deep_research(ActiveRun& active);
    void apply_knowledge_ops(ActiveRun& active, const utils::Json& payload);
    bool deepen_direction(ActiveRun& active, const std::string& direction_id);
    void open_direction(ActiveRun& active, const std::string& label);
    void finalize_research(ActiveRun& active, const std::string& report);
    static std::string make_id(const char* prefix);
    static void normalize_search_target(std::string* module_id, std::string* endpoint);

    storage::Workspace& ws_;
    std::mutex map_mu_;
    std::unordered_map<std::string, std::shared_ptr<ActiveRun>> active_;
};

} // namespace xscope::research
