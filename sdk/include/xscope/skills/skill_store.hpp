#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace xscope::skills {

struct SkillInfo {
    std::string id;          // directory / file stem under skills_root
    std::string name;        // from frontmatter, else id
    std::string description; // from frontmatter (may be empty)
    std::filesystem::path path; // absolute path to SKILL.md or skill file
    std::int64_t updated_at = 0;
    bool is_directory = true;
};

struct SkillDocument {
    SkillInfo info;
    std::string frontmatter; // raw YAML between --- fences (may be empty)
    std::string body;        // markdown / text after frontmatter
    std::string raw;         // full file text
};

class SkillError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// File-based skill manager. Skills live on disk; this module does not embed skill content.
/// Typical layout:
///   skills_root/<id>/SKILL.md
/// Optional single-file skills:
///   skills_root/<id>.md
class SkillStore {
public:
    /// @param skills_root Client- or workspace-provided directory (created if missing).
    void open(const std::filesystem::path& skills_root);
    void close() noexcept;

    bool is_open() const noexcept { return !root_.empty(); }
    const std::filesystem::path& root() const noexcept { return root_; }

    /// Rescan filesystem and refresh index.
    void reload();

    std::vector<SkillInfo> list() const;
    std::optional<SkillInfo> find(const std::string& id) const;
    SkillDocument load(const std::string& id) const;

    /// Copy a skill directory or .md file into skills_root. Returns installed id.
    std::string install(const std::filesystem::path& source);

    /// Write/replace a skill from raw SKILL.md text (creates <id>/SKILL.md).
    void save(const std::string& id, const std::string& raw_markdown);

    void remove(const std::string& id);

    /// UI-bound catalog stream (keyed by skill id).
    std::string catalog_xaiop() const;

private:
    static bool parse_frontmatter(const std::string& raw, std::string& frontmatter, std::string& body,
                                  std::string& name, std::string& description);
    SkillInfo make_info_from_path(const std::filesystem::path& skill_md, const std::string& id,
                                  bool is_directory) const;

    std::filesystem::path root_;
    std::vector<SkillInfo> index_;
};

} // namespace xscope::skills
