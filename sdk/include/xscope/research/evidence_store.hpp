#pragma once

#include "xscope/research/types.hpp"
#include "xscope/storage/database.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace xscope::research {

/// Per-project research tables (project.db schema v2+).
class EvidenceStore {
public:
    void open(storage::Database& project_db, const std::filesystem::path& project_files_dir);
    void close() noexcept;

    void upsert_run(const ResearchRun& run);
    std::optional<ResearchRun> get_run(const std::string& run_id);
    std::vector<ResearchRun> list_runs();

    void upsert_evidence(const EvidenceItem& item);
    std::vector<EvidenceItem> list_evidence(const std::string& run_id);
    std::optional<EvidenceItem> get_evidence(const std::string& evidence_id);

    std::int64_t append_event(const std::string& run_id, const std::string& phase,
                              const std::string& payload_json);

    struct RunEvent {
        std::int64_t seq = 0;
        std::string phase;
        std::string payload_json;
        std::int64_t ts = 0;
    };
    std::vector<RunEvent> list_events(const std::string& run_id, int limit = 0);
    int count_events(const std::string& run_id);

private:
    storage::Database* db_ = nullptr;
    std::filesystem::path files_dir_;
};

} // namespace xscope::research
