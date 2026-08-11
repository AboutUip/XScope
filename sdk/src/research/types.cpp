#include "xscope/research/types.hpp"

#include "xscope/utils/time.hpp"

namespace xscope::research {

const char* run_status_to_string(RunStatus s) noexcept {
    switch (s) {
    case RunStatus::Pending:
        return "pending";
    case RunStatus::Running:
        return "running";
    case RunStatus::WaitingUser:
        return "waiting_user";
    case RunStatus::Synthesizing:
        return "synthesizing";
    case RunStatus::Completed:
        return "completed";
    case RunStatus::Cancelled:
        return "cancelled";
    case RunStatus::Failed:
        return "failed";
    }
    return "pending";
}

RunStatus run_status_from_string(std::string_view s) noexcept {
    if (s == "running") {
        return RunStatus::Running;
    }
    if (s == "waiting_user") {
        return RunStatus::WaitingUser;
    }
    if (s == "synthesizing") {
        return RunStatus::Synthesizing;
    }
    if (s == "completed") {
        return RunStatus::Completed;
    }
    if (s == "cancelled") {
        return RunStatus::Cancelled;
    }
    if (s == "failed") {
        return RunStatus::Failed;
    }
    return RunStatus::Pending;
}

utils::Json make_phase_doc(const ResearchRun& run, const std::string& phase,
                           const utils::Json& payload) {
    utils::Json::Object meta;
    meta.emplace("kind", std::string("research_run"));
    meta.emplace("schema", 1);
    meta.emplace("phase", phase);
    meta.emplace("stream_id", run.id);
    meta.emplace("ts", utils::now_unix_seconds());

    utils::Json::Object root;
    root.emplace("meta", utils::Json(std::move(meta)));
    root.emplace("project_id", run.project_id);
    root.emplace("run_id", run.id);
    root.emplace("query", run.query);
    root.emplace("model_id", run.model_id);
    root.emplace("precision", precision_to_int(run.precision));
    root.emplace("precision_name", std::string(precision_to_string(run.precision)));
    root.emplace("status", std::string(run_status_to_string(run.status)));
    root.emplace("search_rounds_done", run.search_rounds_done);
    root.emplace("ok", phase != "error");
    if (!run.last_error.empty()) {
        root.emplace("error", run.last_error);
    }
    if (!payload.is_null()) {
        root.emplace("payload", payload);
    }
    return utils::Json(std::move(root));
}

} // namespace xscope::research
