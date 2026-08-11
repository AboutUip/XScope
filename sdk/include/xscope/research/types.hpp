#pragma once

#include "xscope/research/precision.hpp"
#include "xscope/utils/json.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace xscope::research {

enum class RunStatus {
    Pending,
    Running,
    WaitingUser,
    Synthesizing,
    Completed,
    Cancelled,
    Failed,
};

const char* run_status_to_string(RunStatus s) noexcept;
RunStatus run_status_from_string(std::string_view s) noexcept;

struct EvidenceItem {
    std::string id;
    std::string run_id;
    std::string kind; // web | github | note
    std::string title;
    std::string source_uri;
    std::string module_id;
    std::string snippet;
    std::string body_json;
    int round = 0;
    std::int64_t created_at = 0;
};

struct ResearchRun {
    std::string id;
    std::string project_id;
    std::string query;
    std::string model_id;
    PrecisionKind precision = PrecisionKind::Normal;
    RunStatus status = RunStatus::Pending;
    int search_rounds_done = 0;
    std::string last_error;
    std::string summary;
    std::string waiting_prompt; // when WaitingUser
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;
};

/// Build a research_run phase document (JSON before XAIOP encode).
utils::Json make_phase_doc(const ResearchRun& run, const std::string& phase,
                           const utils::Json& payload = utils::Json(nullptr));

} // namespace xscope::research
