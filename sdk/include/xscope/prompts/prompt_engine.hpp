#pragma once

#include "xscope/registry/usable_module.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace xscope::prompts {

class PromptError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct PromptContext {
    /// Full usable search modules (already resolved; not re-filtered here).
    std::vector<registry::UsableSearchModule> usable_modules;
    /// Extra placeholders the client may supply (values are inserted as-is, no truncation).
    std::unordered_map<std::string, std::string> extras;
};

/// File-backed prompt templates. Search-module injection is mandatory for chat system prompts.
class PromptEngine {
public:
    static constexpr const char* kChatSystemTemplate = "chat_system";
    static constexpr const char* kResearchSystemTemplate = "research_system";
    static constexpr const char* kPlaceholderSearchModules = "{{search_modules}}";

    /// Open prompts directory (created if missing). Seeds default templates when absent.
    void open(const std::filesystem::path& prompts_root);
    void close() noexcept;

    bool is_open() const noexcept { return !root_.empty(); }
    const std::filesystem::path& root() const noexcept { return root_; }

    void reload();

    /// Load raw template text by id (filename stem, e.g. chat_system → chat_system.md).
    std::string load_template(const std::string& template_id) const;

    void save_template(const std::string& template_id, const std::string& raw);

    /// Render chat system prompt. ALWAYS injects full usable module+skill block.
    /// Throws if the template omits {{search_modules}} (prevents accidental lossy prompts).
    std::string render_chat_system(const PromptContext& ctx) const;

    /// Generic render: replaces known placeholders; requires {{search_modules}} when
    /// require_search_modules is true.
    std::string render(const std::string& template_id, const PromptContext& ctx,
                       bool require_search_modules = true) const;

private:
    void seed_defaults();
    static std::string default_chat_system_template();
    static std::string default_research_system_template();
    static std::string apply_placeholders(std::string templ, const PromptContext& ctx,
                                          const std::string& search_modules_block);

    std::filesystem::path root_;
};

} // namespace xscope::prompts
