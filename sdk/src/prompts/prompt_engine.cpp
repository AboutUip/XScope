#include "xscope/prompts/prompt_engine.hpp"

#include "xscope/utils/utils.hpp"

namespace fs = std::filesystem;

namespace xscope::prompts {

void PromptEngine::open(const fs::path& prompts_root) {
    if (prompts_root.empty()) {
        throw PromptError("prompts_root is empty");
    }
    root_ = fs::absolute(prompts_root);
    xscope::utils::ensure_directory(root_);
    seed_defaults();
}

void PromptEngine::close() noexcept { root_.clear(); }

void PromptEngine::reload() {
    if (root_.empty()) {
        throw PromptError("prompt engine is not open");
    }
    // File-backed; nothing cached yet.
}

void PromptEngine::seed_defaults() {
    const auto path = root_ / (std::string(kChatSystemTemplate) + ".md");
    if (!fs::exists(path)) {
        xscope::utils::write_file_utf8(path, default_chat_system_template());
    }
    const auto research_path = root_ / (std::string(kResearchSystemTemplate) + ".md");
    const auto research_body = default_research_system_template();
    bool rewrite = !fs::exists(research_path);
    if (!rewrite) {
        try {
            const auto existing = xscope::utils::read_file_utf8(research_path);
            if (existing.find("knowledge_get") == std::string::npos &&
                existing.find("Context ownership (MCP / engine actions)") == std::string::npos) {
                rewrite = true;
            }
        } catch (...) {
            rewrite = true;
        }
    }
    if (rewrite) {
        xscope::utils::write_file_utf8(research_path, research_body);
    }
}

std::string PromptEngine::default_chat_system_template() {
    return R"(You are XScope research assistant.

Your goal is precise, evidence-based network research. Prefer completeness over brevity.

## Search modules
The following block is AUTHORITATIVE and COMPLETE. It lists every usable search module and embeds each module's full SKILL.md. Do not omit, summarize, or invent modules.

{{search_modules}}

## Tooling workflow
1. Prefer modules from the injected list above (ids are authoritative).
2. You may call MCP tool `list_search_modules` to refresh the full list (complete skill text included).
3. You may call MCP tool `get_search_module_skill` with a module id to retrieve that module's complete skill again.
4. Follow the skill instructions exactly when using a search module via MCP search tools.
5. Never expose secret values; secrets are resolved inside the SDK.

## Output expectations
Preserve source detail, constraints, and module-specific procedures from the skill text. Do not compress research-critical instructions.
)";
}

std::string PromptEngine::default_research_system_template() {
    return R"(You are the XScope research orchestrator assistant.

## Role & task
- Phase 1 (requirements): turn a fuzzy user input into a CLEAR research need. Searches are for understanding — not final answers.
- Phase 2 (deep research, after lock): investigate until the need is answered, grow the knowledge graph, then synthesize a report.
- Mid-research ask_user is allowed only for necessary unknowns you must not guess.

## Knowledge association graph (MANDATORY ownership)
- The ENGINE NEVER fabricates knowledge-graph nodes or edges. Only YOU write them via action="knowledge".
- Every meaningful, reusable finding that future turns / follow-ups should reference MUST be written into the graph with valid=true (entities, definitions, claims with sources, APIs, protocols, relationships, decisions).
- Emit edges that capture how nodes relate (cites / depends_on / part_of / contradicts / supports / related).
- Do NOT leave the graph empty after solid evidence exists. Skip only noise, duplicates, or unverified guesses (valid=false or omit).
- memory_add is for stage narrative; knowledge is the durable, cross-turn reference graph.

## Context ownership (MCP / engine actions)
The engine does NOT paste prior report bodies into your prompt.
When this project already has stage memory or a knowledge graph (including earlier runs / follow-ups):
- Catalog directories are provided (JSON may be incomplete — that is fine; use ids you need).
- Open bodies with actions memory_get / memory_chain / knowledge_get.
Prefer reading prior findings before new searches on follow-ups.

## Research policy (engine-enforced)
{{research_policy}}

## Current user input
{{query}}

## Search modules
{{search_modules}}

Rules:
- Prefer short, concrete JSON replies as instructed by the user message.
- Always include a complete non-empty "thinking" field (full reasoning for the live feed; never truncate).
- Phase 1 ask_user policy:
  - Ask ONLY when a necessary unknown remains that you must not guess.
  - If uncertain about the user's intent or decisive scope → ask_user (never conjecture).
  - The engine presents ask_user verbatim; there is no ask quota — you decide when to ask or confirm.
  - Use concrete multiple-choice options tied to this query; never ask the user to vaguely restate the need.
- Phase 2: DEPTH = layers along one direction; BREADTH = number of directions. Prefer GitHub REST/code when the need is GitHub-related.
- Twitter / X / 推特 / 推文 / hashtag / @handle / social listening on X → use search module id `twtapi` (see its SKILL). Only when `secret_configured=true`.
- Knowledge nodes: only mark valid=true when the finding is solid enough for the project knowledge graph.
- Whenever you hold a reusable finding, emit action=knowledge (valid=true) with nodes+edges — the engine will not auto-build the graph for you.
)";
}

std::string PromptEngine::load_template(const std::string& template_id) const {
    if (root_.empty()) {
        throw PromptError("prompt engine is not open");
    }
    const auto safe = xscope::utils::sanitize_id(template_id);
    if (safe.empty()) {
        throw PromptError("invalid template id");
    }
    const auto path = root_ / (safe + ".md");
    if (!fs::exists(path)) {
        throw PromptError("template not found: " + safe);
    }
    return xscope::utils::read_file_utf8(path);
}

void PromptEngine::save_template(const std::string& template_id, const std::string& raw) {
    if (root_.empty()) {
        throw PromptError("prompt engine is not open");
    }
    const auto safe = xscope::utils::sanitize_id(template_id);
    if (safe.empty()) {
        throw PromptError("invalid template id");
    }
    xscope::utils::write_file_utf8(root_ / (safe + ".md"), raw);
}

std::string PromptEngine::apply_placeholders(std::string templ, const PromptContext& ctx,
                                             const std::string& search_modules_block) {
    const auto replace_all = [](std::string& s, const std::string& from, const std::string& to) {
        if (from.empty()) {
            return;
        }
        std::size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    };

    replace_all(templ, kPlaceholderSearchModules, search_modules_block);
    for (const auto& [key, value] : ctx.extras) {
        replace_all(templ, "{{" + key + "}}", value);
    }
    return templ;
}

std::string PromptEngine::render(const std::string& template_id, const PromptContext& ctx,
                                 bool require_search_modules) const {
    auto templ = load_template(template_id);
    if (require_search_modules &&
        templ.find(kPlaceholderSearchModules) == std::string::npos) {
        throw PromptError(std::string(template_id) +
                          " must contain {{search_modules}} (full injection is mandatory)");
    }
    const std::string block = registry::format_usable_modules_for_prompt(ctx.usable_modules);
    return apply_placeholders(std::move(templ), ctx, block);
}

std::string PromptEngine::render_chat_system(const PromptContext& ctx) const {
    return render(kChatSystemTemplate, ctx, true);
}

} // namespace xscope::prompts
