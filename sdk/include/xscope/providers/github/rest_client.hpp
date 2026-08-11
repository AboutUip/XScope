#pragma once

#include "xscope/network/http_client.hpp"
#include "xscope/network/types.hpp"
#include "xscope/utils/json.hpp"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xscope::providers::github {

struct RestClientOptions {
    std::string api_base = "https://api.github.com";
    std::string api_version = "2022-11-28";
    std::string user_agent = "XScope-GitHub/0.1";
};

struct RestCall {
    network::HttpMethod method = network::HttpMethod::Get;
    /// Absolute API path beginning with `/`.
    std::string path;
    std::vector<std::pair<std::string, std::string>> query;
    /// When set, used as the full request URL (for Link pagination next targets).
    std::string absolute_url;
    std::string body;
    /// Empty → `application/vnd.github+json`. Examples: raw, diff, patch, html, text-match.
    std::string accept;
    bool text_match = false;
};

struct LinkRelations {
    std::string next;
    std::string prev;
    std::string first;
    std::string last;
};

struct PageCollectOptions {
    int per_page = 100;
    /// Safety cap (accuracy-first, but avoid unbounded loops).
    int max_pages = 100;
};

struct ResponseJsonOptions {
    /// If body is Contents API JSON with base64 `content`, attach full `decoded_content`.
    bool decode_base64_content = false;
};

/// Full-fidelity GitHub REST helper for research (complete bodies, no truncation).
class RestClient {
public:
    using TokenFn = std::function<std::optional<std::string>()>;

    RestClient(network::HttpClient& http, TokenFn token_fn, RestClientOptions options = {});

    const RestClientOptions& options() const noexcept { return options_; }

    network::HttpResponse call(const RestCall& call);

    /// Low-level authenticated request. `path` starts with `/`.
    network::HttpResponse request(network::HttpMethod method, const std::string& path,
                                  const std::vector<std::pair<std::string, std::string>>& query = {},
                                  const std::string& body = {}, bool text_match = false);

    /// Search endpoints: repositories, code, issues, commits, users, topics, labels.
    network::HttpResponse search(const std::string& endpoint, const std::string& q,
                                 const std::vector<std::pair<std::string, std::string>>& extra_query = {},
                                 bool text_match = false);

    // --- Research-oriented resource helpers (read) ---
    network::HttpResponse get_rate_limit();
    network::HttpResponse get_repo(const std::string& owner, const std::string& repo);
    network::HttpResponse get_readme(const std::string& owner, const std::string& repo,
                                     const std::string& ref = {});
    network::HttpResponse get_contents(const std::string& owner, const std::string& repo,
                                       const std::string& path, const std::string& ref = {});
    /// Accept: application/vnd.github.raw — body is file bytes/text, not JSON metadata.
    network::HttpResponse get_raw_contents(const std::string& owner, const std::string& repo,
                                           const std::string& path, const std::string& ref = {});
    network::HttpResponse get_git_tree(const std::string& owner, const std::string& repo,
                                       const std::string& tree_sha, bool recursive = true);
    network::HttpResponse get_git_blob(const std::string& owner, const std::string& repo,
                                       const std::string& file_sha);
    network::HttpResponse get_git_ref(const std::string& owner, const std::string& repo,
                                      const std::string& ref);
    network::HttpResponse get_commit(const std::string& owner, const std::string& repo,
                                     const std::string& ref);
    network::HttpResponse list_commits(const std::string& owner, const std::string& repo,
                                       const std::vector<std::pair<std::string, std::string>>& query = {});
    network::HttpResponse compare(const std::string& owner, const std::string& repo,
                                  const std::string& base, const std::string& head);
    network::HttpResponse get_issue(const std::string& owner, const std::string& repo, int number);
    network::HttpResponse list_issues(const std::string& owner, const std::string& repo,
                                      const std::vector<std::pair<std::string, std::string>>& query = {});
    network::HttpResponse list_issue_comments(const std::string& owner, const std::string& repo,
                                              int number,
                                              const std::vector<std::pair<std::string, std::string>>& query = {});
    network::HttpResponse get_pull(const std::string& owner, const std::string& repo, int number);
    network::HttpResponse list_pulls(const std::string& owner, const std::string& repo,
                                     const std::vector<std::pair<std::string, std::string>>& query = {});
    network::HttpResponse list_pull_files(const std::string& owner, const std::string& repo, int number,
                                          const std::vector<std::pair<std::string, std::string>>& query = {});
    network::HttpResponse list_pull_commits(const std::string& owner, const std::string& repo, int number,
                                            const std::vector<std::pair<std::string, std::string>>& query = {});
    network::HttpResponse get_release(const std::string& owner, const std::string& repo,
                                      std::int64_t release_id);
    network::HttpResponse get_release_by_tag(const std::string& owner, const std::string& repo,
                                             const std::string& tag);
    network::HttpResponse get_latest_release(const std::string& owner, const std::string& repo);
    network::HttpResponse list_releases(const std::string& owner, const std::string& repo,
                                        const std::vector<std::pair<std::string, std::string>>& query = {});
    network::HttpResponse list_branches(const std::string& owner, const std::string& repo,
                                        const std::vector<std::pair<std::string, std::string>>& query = {});
    network::HttpResponse list_tags(const std::string& owner, const std::string& repo,
                                    const std::vector<std::pair<std::string, std::string>>& query = {});
    network::HttpResponse get_languages(const std::string& owner, const std::string& repo);
    network::HttpResponse list_contributors(const std::string& owner, const std::string& repo,
                                            const std::vector<std::pair<std::string, std::string>>& query = {});
    network::HttpResponse list_forks(const std::string& owner, const std::string& repo,
                                     const std::vector<std::pair<std::string, std::string>>& query = {});
    network::HttpResponse get_user(const std::string& username = {});
    network::HttpResponse get_org(const std::string& org);
    network::HttpResponse get_repo_topics(const std::string& owner, const std::string& repo);

    /// Follow `Link: rel="next"` until exhausted or max_pages. Each page body is kept complete.
    std::vector<network::HttpResponse> collect_pages(
        const std::string& path, std::vector<std::pair<std::string, std::string>> query = {},
        PageCollectOptions page_opts = {}, const std::string& accept = {});

    /// Named resource dispatch used by MCP `github_resource`.
    network::HttpResponse resource(const std::string& name, const utils::Json& args);

    static LinkRelations parse_link_header(std::string_view link_header);
    static std::optional<std::string> find_header(const network::HeaderList& headers,
                                                  std::string_view name);
    /// Join `/`-separated segments with per-segment percent-encoding (slashes preserved as separators).
    static std::string encode_path(std::string_view path);
    static std::string repo_path(const std::string& owner, const std::string& repo,
                                 std::string_view suffix = {});

    static utils::Json response_to_json(const network::HttpResponse& resp,
                                        ResponseJsonOptions enrich = {});
    /// `{ page_count, pages:[...], items?: combined array if every page body is a JSON array }`
    static utils::Json pages_to_json(const std::vector<network::HttpResponse>& pages,
                                     ResponseJsonOptions enrich = {});

    /// Machine-readable catalog of named resources (for AI / UI).
    static utils::Json resource_catalog_json();

private:
    void apply_headers(network::HttpRequest& req, const RestCall& call) const;
    static std::vector<std::pair<std::string, std::string>> query_from_json(const utils::Json& args);
    static void require_owner_repo(const utils::Json& args, std::string& owner, std::string& repo);

    network::HttpClient& http_;
    TokenFn token_fn_;
    RestClientOptions options_;
};

} // namespace xscope::providers::github
