#include "xscope/providers/github/builtin.hpp"

namespace xscope::providers::github {

const char* github_skill_markdown() {
    return R"MD(---
name: github-search
description: >
  GitHub REST API research via OAuth device flow (or PAT fallback).
  Prefer full search responses and follow-up contents/repo GETs; never summarize away evidence.
---

# GitHub search (XScope)

Use this module when the research question benefits from GitHub repositories, source code,
issues/PRs, commits, users, topics, or labels.

## Authentication (required for serious use)

1. Prefer OAuth device flow tools:
   - `github_oauth_start` → show `user_code`, open `verification_uri`
   - poll `github_oauth_poll` with `device_code` every `interval` seconds until `authorized`
2. Fallback: `github_oauth_set_pat` with a fine-grained or classic PAT.
3. Check `github_oauth_status` (never expect the raw token in tool output).
4. Disconnect with `github_oauth_disconnect`.

OAuth App requirements (operator):
- Create a GitHub OAuth App (or GitHub App with device flow enabled).
- Enable **Device Flow** in the app settings.
- Configure XScope with `XSCOPE_GITHUB_OAUTH_CLIENT_ID` (and optional secret) or
  `data_root/global/github_oauth.json` `{ "client_id": "..." }`.
- Default scope: `read:user`. Request `repo` (or finer permissions) when private data is needed.

**Code search (`endpoint=code`) always requires a connected token.**

## Tools

Call `github_rest_catalog` for the machine-readable named-resource list.

### `run_search` (module_id = `github`)

Arguments:
- `module_id`: must be `"github"`
- `endpoint`: one of `repositories`, `code`, `issues`, `commits`, `users`, `topics`, `labels`
- `q`: full GitHub search query string (qualifiers allowed). Do not truncate the query for token savings.
- optional: `sort`, `order`, `page`, `per_page` (max 100), `text_match` (bool),
  `search_type` (`semantic`|`hybrid`, issues only), `advanced_search`

Returns the **complete** HTTP JSON body plus rate-limit headers. Honor:
- `incomplete_results`
- `total_count` vs returned `items` length
- pagination up to GitHub's **1000-result** search cap (`page` * `per_page`)
- `text_matches` when `text_match=true`

### `github_resource` (preferred for deep reads)

Named helpers (full fidelity). Common ids:
`rate_limit`, `user`, `org`, `repo`, `readme`, `contents`, `raw`, `git_tree`, `git_blob`,
`git_ref`, `commit`, `commits`, `compare`, `issue`, `issues`, `issue_comments`,
`pull`, `pulls`, `pull_files`, `pull_commits`, `release`, `latest_release`, `releases`,
`branches`, `tags`, `languages`, `contributors`, `forks`, `topics`.

Useful flags:
- `decode_content=true` — attach full `decoded_content` for base64 Contents/Blob JSON (default on for contents/readme/git_blob)
- `paginate=true` — follow `Link: rel=next` for list resources; **every page body is kept**; JSON arrays are also concatenated into `items`

### `github_rest` / `github_rest_get`

Arbitrary authenticated REST:
- `path` starting with `/`
- optional `method` (`get` default; also head/post/put/patch/delete)
- optional `query`, `body`, `accept` (e.g. `application/vnd.github.raw`, `.diff`, `.patch`)
- optional `text_match`, `decode_content`

### `github_rest_paginate`

GET `path` and follow pagination links until exhausted or `max_pages`.
Returns `{ page_count, pages:[complete...], items?, item_count? }` — do not drop pages for token savings.

Contents API: files ≤1MB return base64 `content` — use `decode_content` or `resource=raw`.
Larger files: `git_blob` after resolving sha from `git_tree`.

## Query construction (do not oversimplify)

Format: `KEYWORDS QUALIFIER:value ...`

Useful qualifiers (non-exhaustive):
- Repositories: `in:name,description,readme`, `user:`, `org:`, `language:`, `stars:`, `fork:`, `topic:`
- Code: `in:file,path`, `language:`, `repo:`, `path:`, `extension:` (always include a search term; default branch only; files < 384KB)
- Issues/PRs: `is:issue`, `is:pr`, `state:`, `label:`, `author:`, `repo:`, `involves:`
- Commits: `author:`, `committer:`, `repo:`, `merge:true|false`
- Users: `type:user|org`, `in:login,name`

Limits:
- Keyword portion ≤ 256 characters; ≤ 5 `AND`/`OR`/`NOT` operators
- Search may time out → `incomplete_results=true` (still use returned items; refine query and continue)
- Inaccessible `repo:`/`org:` qualifiers may 422 or silently omit results

## Research accuracy policy

- Prefer **more complete evidence** over shorter answers.
- Keep raw API payloads (or full extracted file text) in the research project when they support claims.
- Do not summarize SKILL content or strip search hits to save tokens.
- When listing candidates, retain identifiers (`full_name`, `html_url`, `sha`, `path`, issue numbers) for follow-up GETs.
- Watch rate limits: `search` ≈ 30/min authenticated (code_search ≈ 10/min); `core` for contents/repo GETs.

## Suggested workflow

1. Confirm connection via `github_oauth_status`.
2. Broad `run_search` on `repositories` or `issues` with precise qualifiers.
3. Narrow with `code` search when looking for concrete implementations.
4. Use `github_resource` (`repo`, `contents`/`raw`, `git_tree`, `issue`, `pull_files`, …) for full documents.
5. Use `github_rest_paginate` / `paginate=true` when list endpoints span multiple pages.
6. Record URLs and SHAs so results are reproducible.
)MD";
}

void ensure_github_search_module(skills::SkillStore& skills, registry::SearchRegistry& registry) {
    skills.save("github-search", github_skill_markdown());

    registry::SearchModule mod;
    mod.id = "github";
    mod.name = "GitHub";
    mod.description =
        "GitHub REST search (repositories/code/issues/commits/users/topics/labels) via OAuth device flow";
    mod.enabled = true;
    mod.skill_id = "github-search";
    mod.requires_api_key = true;
    mod.auth.type = registry::AuthType::OAuth;
    mod.auth.secret_id = "github.oauth";
    mod.auth.param_name = "Authorization";
    mod.tags = {"github", "code", "issues", "repos", "oauth"};
    registry.upsert(mod);
    registry.save();
}

} // namespace xscope::providers::github
