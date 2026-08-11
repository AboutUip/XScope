#include "xscope/providers/github/rest_client.hpp"

#include "xscope/auth/types.hpp"
#include "xscope/utils/base64.hpp"
#include "xscope/utils/string.hpp"
#include "xscope/utils/url.hpp"

#include <cctype>
#include <chrono>

namespace xscope::providers::github {
namespace {

void maybe_decode_contents_node(utils::Json& node) {
    if (!node.is_object()) {
        return;
    }
    if (!node.contains("encoding") || !node.contains("content")) {
        return;
    }
    if (node.at("encoding").as_string("") != "base64") {
        return;
    }
    try {
        const auto decoded = utils::base64_decode_string(node.at("content").as_string(""));
        node["decoded_content"] = decoded;
        node["decoded_bytes"] = static_cast<std::int64_t>(decoded.size());
    } catch (...) {
        node["decoded_content_error"] = std::string("base64 decode failed");
    }
}

void enrich_body_json(utils::Json& body, const ResponseJsonOptions& enrich) {
    if (!enrich.decode_base64_content) {
        return;
    }
    if (body.is_object()) {
        maybe_decode_contents_node(body);
        // Git blob API also uses encoding/content.
        return;
    }
    if (body.is_array()) {
        for (auto& item : body.as_array()) {
            if (item.is_object()) {
                maybe_decode_contents_node(item);
            }
        }
    }
}

std::string lower_copy(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

} // namespace

RestClient::RestClient(network::HttpClient& http, TokenFn token_fn, RestClientOptions options)
    : http_(http), token_fn_(std::move(token_fn)), options_(std::move(options)) {}

void RestClient::apply_headers(network::HttpRequest& req, const RestCall& call) const {
    if (call.text_match) {
        req.headers.emplace_back("Accept", "application/vnd.github.text-match+json");
    } else if (!call.accept.empty()) {
        req.headers.emplace_back("Accept", call.accept);
    } else {
        req.headers.emplace_back("Accept", "application/vnd.github+json");
    }
    req.headers.emplace_back("X-GitHub-Api-Version", options_.api_version);
    req.headers.emplace_back("User-Agent", options_.user_agent);
    if (token_fn_) {
        if (auto token = token_fn_()) {
            if (!token->empty()) {
                req.headers.emplace_back("Authorization", "Bearer " + *token);
            }
        }
    }
}

network::HttpResponse RestClient::call(const RestCall& call) {
    network::HttpRequest req;
    req.method = call.method;
    if (!call.absolute_url.empty()) {
        req.url = call.absolute_url;
    } else {
        if (call.path.empty() || call.path.front() != '/') {
            throw network::NetworkError("GitHub REST path must start with '/'");
        }
        req.url = utils::url_with_query(options_.api_base + call.path, call.query);
    }
    req.body = call.body;
    if (!call.body.empty() && call.method != network::HttpMethod::Get &&
        call.method != network::HttpMethod::Head) {
        req.headers.emplace_back("Content-Type", "application/json");
    }
    apply_headers(req, call);
    req.timeout = std::chrono::seconds(60);
    return http_.send(req);
}

network::HttpResponse RestClient::request(network::HttpMethod method, const std::string& path,
                                          const std::vector<std::pair<std::string, std::string>>& query,
                                          const std::string& body, bool text_match) {
    RestCall c;
    c.method = method;
    c.path = path;
    c.query = query;
    c.body = body;
    c.text_match = text_match;
    return call(c);
}

network::HttpResponse RestClient::search(
    const std::string& endpoint, const std::string& q,
    const std::vector<std::pair<std::string, std::string>>& extra_query, bool text_match) {
    if (q.empty()) {
        throw network::NetworkError("GitHub search requires non-empty q");
    }
    static const char* kAllowed[] = {"repositories", "code", "issues", "commits",
                                     "users",        "topics", "labels"};
    bool ok = false;
    for (const char* name : kAllowed) {
        if (endpoint == name) {
            ok = true;
            break;
        }
    }
    if (!ok) {
        throw network::NetworkError("unsupported GitHub search endpoint: " + endpoint);
    }
    if (endpoint == "code") {
        if (!token_fn_ || !token_fn_()) {
            throw auth::AuthError("GitHub code search requires authentication");
        }
    }
    std::vector<std::pair<std::string, std::string>> query;
    query.emplace_back("q", q);
    for (const auto& kv : extra_query) {
        if (kv.first == "q") {
            continue;
        }
        query.push_back(kv);
    }
    return request(network::HttpMethod::Get, "/search/" + endpoint, query, {}, text_match);
}

std::string RestClient::encode_path(std::string_view path) {
    std::string out;
    size_t i = 0;
    while (i < path.size() && path[i] == '/') {
        out.push_back('/');
        ++i;
    }
    while (i < path.size()) {
        const size_t slash = path.find('/', i);
        const auto seg = path.substr(i, slash == std::string_view::npos ? path.size() - i : slash - i);
        out += utils::url_encode(seg);
        if (slash == std::string_view::npos) {
            break;
        }
        out.push_back('/');
        i = slash + 1;
    }
    return out;
}

std::string RestClient::repo_path(const std::string& owner, const std::string& repo,
                                  std::string_view suffix) {
    std::string p = "/repos/" + utils::url_encode(owner) + "/" + utils::url_encode(repo);
    if (!suffix.empty()) {
        if (suffix.front() != '/') {
            p.push_back('/');
        }
        p += encode_path(suffix);
    }
    return p;
}

network::HttpResponse RestClient::get_rate_limit() {
    return request(network::HttpMethod::Get, "/rate_limit");
}
network::HttpResponse RestClient::get_repo(const std::string& owner, const std::string& repo) {
    return request(network::HttpMethod::Get, repo_path(owner, repo));
}
network::HttpResponse RestClient::get_readme(const std::string& owner, const std::string& repo,
                                             const std::string& ref) {
    std::vector<std::pair<std::string, std::string>> q;
    if (!ref.empty()) {
        q.emplace_back("ref", ref);
    }
    return request(network::HttpMethod::Get, repo_path(owner, repo, "/readme"), q);
}
network::HttpResponse RestClient::get_contents(const std::string& owner, const std::string& repo,
                                               const std::string& path, const std::string& ref) {
    std::vector<std::pair<std::string, std::string>> q;
    if (!ref.empty()) {
        q.emplace_back("ref", ref);
    }
    if (path.empty()) {
        return request(network::HttpMethod::Get, repo_path(owner, repo, "/contents"), q);
    }
    return request(network::HttpMethod::Get, repo_path(owner, repo, "/contents/" + path), q);
}
network::HttpResponse RestClient::get_raw_contents(const std::string& owner, const std::string& repo,
                                                   const std::string& path, const std::string& ref) {
    RestCall c;
    c.method = network::HttpMethod::Get;
    c.path = repo_path(owner, repo, "/contents/" + path);
    if (!ref.empty()) {
        c.query.emplace_back("ref", ref);
    }
    c.accept = "application/vnd.github.raw";
    return call(c);
}
network::HttpResponse RestClient::get_git_tree(const std::string& owner, const std::string& repo,
                                               const std::string& tree_sha, bool recursive) {
    std::vector<std::pair<std::string, std::string>> q;
    if (recursive) {
        q.emplace_back("recursive", "1");
    }
    return request(network::HttpMethod::Get, repo_path(owner, repo, "/git/trees/" + tree_sha), q);
}
network::HttpResponse RestClient::get_git_blob(const std::string& owner, const std::string& repo,
                                               const std::string& file_sha) {
    return request(network::HttpMethod::Get, repo_path(owner, repo, "/git/blobs/" + file_sha));
}
network::HttpResponse RestClient::get_git_ref(const std::string& owner, const std::string& repo,
                                              const std::string& ref) {
    // ref like "heads/main" or "tags/v1.0"
    return request(network::HttpMethod::Get, repo_path(owner, repo, "/git/ref/" + ref));
}
network::HttpResponse RestClient::get_commit(const std::string& owner, const std::string& repo,
                                             const std::string& ref) {
    return request(network::HttpMethod::Get, repo_path(owner, repo, "/commits/" + ref));
}
network::HttpResponse RestClient::list_commits(
    const std::string& owner, const std::string& repo,
    const std::vector<std::pair<std::string, std::string>>& query) {
    return request(network::HttpMethod::Get, repo_path(owner, repo, "/commits"), query);
}
network::HttpResponse RestClient::compare(const std::string& owner, const std::string& repo,
                                          const std::string& base, const std::string& head) {
    const std::string path = "/repos/" + utils::url_encode(owner) + "/" + utils::url_encode(repo) +
                             "/compare/" + utils::url_encode(base) + "..." + utils::url_encode(head);
    return request(network::HttpMethod::Get, path);
}
network::HttpResponse RestClient::get_issue(const std::string& owner, const std::string& repo,
                                            int number) {
    return request(network::HttpMethod::Get,
                   repo_path(owner, repo, "/issues/" + std::to_string(number)));
}
network::HttpResponse RestClient::list_issues(
    const std::string& owner, const std::string& repo,
    const std::vector<std::pair<std::string, std::string>>& query) {
    return request(network::HttpMethod::Get, repo_path(owner, repo, "/issues"), query);
}
network::HttpResponse RestClient::list_issue_comments(
    const std::string& owner, const std::string& repo, int number,
    const std::vector<std::pair<std::string, std::string>>& query) {
    return request(network::HttpMethod::Get,
                   repo_path(owner, repo, "/issues/" + std::to_string(number) + "/comments"), query);
}
network::HttpResponse RestClient::get_pull(const std::string& owner, const std::string& repo,
                                           int number) {
    return request(network::HttpMethod::Get,
                   repo_path(owner, repo, "/pulls/" + std::to_string(number)));
}
network::HttpResponse RestClient::list_pulls(
    const std::string& owner, const std::string& repo,
    const std::vector<std::pair<std::string, std::string>>& query) {
    return request(network::HttpMethod::Get, repo_path(owner, repo, "/pulls"), query);
}
network::HttpResponse RestClient::list_pull_files(
    const std::string& owner, const std::string& repo, int number,
    const std::vector<std::pair<std::string, std::string>>& query) {
    return request(network::HttpMethod::Get,
                   repo_path(owner, repo, "/pulls/" + std::to_string(number) + "/files"), query);
}
network::HttpResponse RestClient::list_pull_commits(
    const std::string& owner, const std::string& repo, int number,
    const std::vector<std::pair<std::string, std::string>>& query) {
    return request(network::HttpMethod::Get,
                   repo_path(owner, repo, "/pulls/" + std::to_string(number) + "/commits"), query);
}
network::HttpResponse RestClient::get_release(const std::string& owner, const std::string& repo,
                                              std::int64_t release_id) {
    return request(network::HttpMethod::Get,
                   repo_path(owner, repo, "/releases/" + std::to_string(release_id)));
}
network::HttpResponse RestClient::get_release_by_tag(const std::string& owner, const std::string& repo,
                                                     const std::string& tag) {
    return request(network::HttpMethod::Get, repo_path(owner, repo, "/releases/tags/" + tag));
}
network::HttpResponse RestClient::get_latest_release(const std::string& owner, const std::string& repo) {
    return request(network::HttpMethod::Get, repo_path(owner, repo, "/releases/latest"));
}
network::HttpResponse RestClient::list_releases(
    const std::string& owner, const std::string& repo,
    const std::vector<std::pair<std::string, std::string>>& query) {
    return request(network::HttpMethod::Get, repo_path(owner, repo, "/releases"), query);
}
network::HttpResponse RestClient::list_branches(
    const std::string& owner, const std::string& repo,
    const std::vector<std::pair<std::string, std::string>>& query) {
    return request(network::HttpMethod::Get, repo_path(owner, repo, "/branches"), query);
}
network::HttpResponse RestClient::list_tags(
    const std::string& owner, const std::string& repo,
    const std::vector<std::pair<std::string, std::string>>& query) {
    return request(network::HttpMethod::Get, repo_path(owner, repo, "/tags"), query);
}
network::HttpResponse RestClient::get_languages(const std::string& owner, const std::string& repo) {
    return request(network::HttpMethod::Get, repo_path(owner, repo, "/languages"));
}
network::HttpResponse RestClient::list_contributors(
    const std::string& owner, const std::string& repo,
    const std::vector<std::pair<std::string, std::string>>& query) {
    return request(network::HttpMethod::Get, repo_path(owner, repo, "/contributors"), query);
}
network::HttpResponse RestClient::list_forks(
    const std::string& owner, const std::string& repo,
    const std::vector<std::pair<std::string, std::string>>& query) {
    return request(network::HttpMethod::Get, repo_path(owner, repo, "/forks"), query);
}
network::HttpResponse RestClient::get_user(const std::string& username) {
    if (username.empty()) {
        return request(network::HttpMethod::Get, "/user");
    }
    return request(network::HttpMethod::Get, "/users/" + utils::url_encode(username));
}
network::HttpResponse RestClient::get_org(const std::string& org) {
    return request(network::HttpMethod::Get, "/orgs/" + utils::url_encode(org));
}
network::HttpResponse RestClient::get_repo_topics(const std::string& owner, const std::string& repo) {
    RestCall c;
    c.method = network::HttpMethod::Get;
    c.path = repo_path(owner, repo, "/topics");
    c.accept = "application/vnd.github+json";
    return call(c);
}

std::optional<std::string> RestClient::find_header(const network::HeaderList& headers,
                                                   std::string_view name) {
    const auto want = lower_copy(name);
    for (const auto& h : headers) {
        if (lower_copy(h.first) == want) {
            return h.second;
        }
    }
    return std::nullopt;
}

LinkRelations RestClient::parse_link_header(std::string_view link_header) {
    LinkRelations out;
    size_t i = 0;
    while (i < link_header.size()) {
        while (i < link_header.size() && (link_header[i] == ' ' || link_header[i] == ',')) {
            ++i;
        }
        if (i >= link_header.size() || link_header[i] != '<') {
            break;
        }
        ++i;
        const size_t end = link_header.find('>', i);
        if (end == std::string_view::npos) {
            break;
        }
        const std::string url(link_header.substr(i, end - i));
        i = end + 1;
        // ; rel="next"
        while (i < link_header.size() && link_header[i] != ',' && link_header[i] != '<') {
            while (i < link_header.size() && (link_header[i] == ' ' || link_header[i] == ';')) {
                ++i;
            }
            if (i + 4 <= link_header.size() && lower_copy(link_header.substr(i, 3)) == "rel") {
                i += 3;
                while (i < link_header.size() && (link_header[i] == ' ' || link_header[i] == '=')) {
                    ++i;
                }
                if (i < link_header.size() && (link_header[i] == '"' || link_header[i] == '\'')) {
                    const char quote = link_header[i++];
                    const size_t qend = link_header.find(quote, i);
                    if (qend == std::string_view::npos) {
                        break;
                    }
                    const auto rel = std::string(link_header.substr(i, qend - i));
                    if (rel == "next") {
                        out.next = url;
                    } else if (rel == "prev") {
                        out.prev = url;
                    } else if (rel == "first") {
                        out.first = url;
                    } else if (rel == "last") {
                        out.last = url;
                    }
                    i = qend + 1;
                }
            } else {
                while (i < link_header.size() && link_header[i] != ',' && link_header[i] != ';' &&
                       link_header[i] != '<') {
                    ++i;
                }
            }
        }
    }
    return out;
}

std::vector<network::HttpResponse> RestClient::collect_pages(
    const std::string& path, std::vector<std::pair<std::string, std::string>> query,
    PageCollectOptions page_opts, const std::string& accept) {
    if (page_opts.per_page <= 0) {
        page_opts.per_page = 100;
    }
    if (page_opts.per_page > 100) {
        page_opts.per_page = 100;
    }
    if (page_opts.max_pages <= 0) {
        page_opts.max_pages = 1;
    }
    bool has_per_page = false;
    for (const auto& kv : query) {
        if (kv.first == "per_page") {
            has_per_page = true;
            break;
        }
    }
    if (!has_per_page) {
        query.emplace_back("per_page", std::to_string(page_opts.per_page));
    }

    std::vector<network::HttpResponse> pages;
    RestCall first;
    first.method = network::HttpMethod::Get;
    first.path = path;
    first.query = query;
    first.accept = accept;
    auto resp = call(first);
    pages.push_back(std::move(resp));

    for (int n = 1; n < page_opts.max_pages; ++n) {
        const auto link = find_header(pages.back().headers, "link");
        if (!link) {
            break;
        }
        const auto rels = parse_link_header(*link);
        if (rels.next.empty()) {
            break;
        }
        RestCall next_call;
        next_call.method = network::HttpMethod::Get;
        next_call.absolute_url = rels.next;
        next_call.accept = accept;
        pages.push_back(call(next_call));
        if (pages.back().status < 200 || pages.back().status >= 300) {
            break;
        }
    }
    return pages;
}

std::vector<std::pair<std::string, std::string>> RestClient::query_from_json(const utils::Json& args) {
    std::vector<std::pair<std::string, std::string>> query;
    if (args.contains("query") && args.at("query").is_object()) {
        for (const auto& [k, v] : args.at("query").as_object()) {
            if (v.is_string()) {
                query.emplace_back(k, v.as_string());
            } else if (v.is_number()) {
                query.emplace_back(k, std::to_string(v.as_int64(0)));
            } else if (v.is_bool()) {
                query.emplace_back(k, v.as_bool() ? "true" : "false");
            } else {
                query.emplace_back(k, v.dump(0));
            }
        }
    }
    // Flat common pagination fields.
    for (const char* key : {"page", "per_page", "state", "sort", "direction", "sha", "path", "ref",
                            "since", "until", "labels", "base", "head"}) {
        if (args.contains(key) && !args.at(key).is_null()) {
            const auto& v = args.at(key);
            std::string s;
            if (v.is_string()) {
                s = v.as_string();
            } else if (v.is_number()) {
                s = std::to_string(v.as_int64(0));
            } else if (v.is_bool()) {
                s = v.as_bool() ? "true" : "false";
            } else {
                continue;
            }
            if (!s.empty()) {
                bool exists = false;
                for (const auto& kv : query) {
                    if (kv.first == key) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    query.emplace_back(key, s);
                }
            }
        }
    }
    return query;
}

void RestClient::require_owner_repo(const utils::Json& args, std::string& owner, std::string& repo) {
    owner = args.contains("owner") ? args.at("owner").as_string("") : "";
    repo = args.contains("repo") ? args.at("repo").as_string("") : "";
    if (owner.empty() || repo.empty()) {
        throw network::NetworkError("owner and repo are required");
    }
}

network::HttpResponse RestClient::resource(const std::string& name, const utils::Json& args) {
    auto q = query_from_json(args);
    std::string owner;
    std::string repo;

    if (name == "rate_limit") {
        return get_rate_limit();
    }
    if (name == "user") {
        const std::string username = args.contains("username") ? args.at("username").as_string("") : "";
        return get_user(username);
    }
    if (name == "org") {
        const std::string org = args.contains("org") ? args.at("org").as_string("") : "";
        if (org.empty()) {
            throw network::NetworkError("org is required");
        }
        return get_org(org);
    }

    require_owner_repo(args, owner, repo);
    const std::string ref = args.contains("ref") ? args.at("ref").as_string("") : "";
    const std::string path = args.contains("path") ? args.at("path").as_string("") : "";
    const int number = args.contains("number") ? static_cast<int>(args.at("number").as_int64(0)) : 0;

    if (name == "repo") {
        return get_repo(owner, repo);
    }
    if (name == "readme") {
        return get_readme(owner, repo, ref);
    }
    if (name == "contents") {
        if (path.empty()) {
            // root listing
            return get_contents(owner, repo, "", ref);
        }
        return get_contents(owner, repo, path, ref);
    }
    if (name == "raw") {
        if (path.empty()) {
            throw network::NetworkError("path is required for raw");
        }
        return get_raw_contents(owner, repo, path, ref);
    }
    if (name == "git_tree") {
        std::string sha = args.contains("tree_sha") ? args.at("tree_sha").as_string("") : "";
        if (sha.empty()) {
            sha = args.contains("sha") ? args.at("sha").as_string("") : "";
        }
        if (sha.empty()) {
            throw network::NetworkError("tree_sha is required");
        }
        const bool recursive = !args.contains("recursive") || args.at("recursive").as_bool(true);
        return get_git_tree(owner, repo, sha, recursive);
    }
    if (name == "git_blob") {
        const std::string sha = args.contains("file_sha") ? args.at("file_sha").as_string("")
                              : args.contains("sha")     ? args.at("sha").as_string("")
                                                         : "";
        if (sha.empty()) {
            throw network::NetworkError("file_sha is required");
        }
        return get_git_blob(owner, repo, sha);
    }
    if (name == "git_ref") {
        const std::string r = args.contains("git_ref") ? args.at("git_ref").as_string("")
                            : args.contains("ref")     ? args.at("ref").as_string("")
                                                       : "";
        if (r.empty()) {
            throw network::NetworkError("git_ref is required (e.g. heads/main)");
        }
        return get_git_ref(owner, repo, r);
    }
    if (name == "commit") {
        const std::string r = !ref.empty() ? ref : (args.contains("sha") ? args.at("sha").as_string("") : "");
        if (r.empty()) {
            throw network::NetworkError("ref/sha is required");
        }
        return get_commit(owner, repo, r);
    }
    if (name == "commits") {
        return list_commits(owner, repo, q);
    }
    if (name == "compare") {
        const std::string base = args.contains("base") ? args.at("base").as_string("") : "";
        const std::string head = args.contains("head") ? args.at("head").as_string("") : "";
        if (base.empty() || head.empty()) {
            throw network::NetworkError("base and head are required");
        }
        return compare(owner, repo, base, head);
    }
    if (name == "issue") {
        if (number <= 0) {
            throw network::NetworkError("number is required");
        }
        return get_issue(owner, repo, number);
    }
    if (name == "issues") {
        return list_issues(owner, repo, q);
    }
    if (name == "issue_comments") {
        if (number <= 0) {
            throw network::NetworkError("number is required");
        }
        return list_issue_comments(owner, repo, number, q);
    }
    if (name == "pull") {
        if (number <= 0) {
            throw network::NetworkError("number is required");
        }
        return get_pull(owner, repo, number);
    }
    if (name == "pulls") {
        return list_pulls(owner, repo, q);
    }
    if (name == "pull_files") {
        if (number <= 0) {
            throw network::NetworkError("number is required");
        }
        return list_pull_files(owner, repo, number, q);
    }
    if (name == "pull_commits") {
        if (number <= 0) {
            throw network::NetworkError("number is required");
        }
        return list_pull_commits(owner, repo, number, q);
    }
    if (name == "release") {
        if (args.contains("release_id")) {
            return get_release(owner, repo, args.at("release_id").as_int64(0));
        }
        const std::string tag = args.contains("tag") ? args.at("tag").as_string("") : "";
        if (tag.empty()) {
            throw network::NetworkError("release_id or tag is required");
        }
        return get_release_by_tag(owner, repo, tag);
    }
    if (name == "latest_release") {
        return get_latest_release(owner, repo);
    }
    if (name == "releases") {
        return list_releases(owner, repo, q);
    }
    if (name == "branches") {
        return list_branches(owner, repo, q);
    }
    if (name == "tags") {
        return list_tags(owner, repo, q);
    }
    if (name == "languages") {
        return get_languages(owner, repo);
    }
    if (name == "contributors") {
        return list_contributors(owner, repo, q);
    }
    if (name == "forks") {
        return list_forks(owner, repo, q);
    }
    if (name == "topics") {
        return get_repo_topics(owner, repo);
    }
    throw network::NetworkError("unknown GitHub resource: " + name);
}

utils::Json RestClient::response_to_json(const network::HttpResponse& resp,
                                         ResponseJsonOptions enrich) {
    utils::Json::Object obj;
    obj.emplace("status", resp.status);
    utils::Json::Object headers;
    for (const auto& h : resp.headers) {
        const auto lower = lower_copy(h.first);
        if (lower == "x-ratelimit-limit" || lower == "x-ratelimit-remaining" ||
            lower == "x-ratelimit-reset" || lower == "x-ratelimit-used" ||
            lower == "x-ratelimit-resource" || lower == "retry-after" || lower == "link" ||
            lower == "x-github-request-id" || lower == "content-type" || lower == "etag" ||
            lower == "last-modified" || lower == "location") {
            headers.emplace(lower, h.second);
        }
    }
    obj.emplace("headers", utils::Json(std::move(headers)));
    const auto trimmed = utils::trim_copy(resp.body);
    if (!trimmed.empty() && (trimmed.front() == '{' || trimmed.front() == '[')) {
        try {
            auto body = utils::Json::parse(resp.body);
            enrich_body_json(body, enrich);
            obj.emplace("body", std::move(body));
            obj.emplace("body_format", std::string("json"));
        } catch (...) {
            obj.emplace("body", resp.body);
            obj.emplace("body_format", std::string("text"));
        }
    } else {
        obj.emplace("body", resp.body);
        obj.emplace("body_format", std::string("text"));
    }
    obj.emplace("body_bytes", static_cast<std::int64_t>(resp.body.size()));
    return utils::Json(std::move(obj));
}

utils::Json RestClient::pages_to_json(const std::vector<network::HttpResponse>& pages,
                                      ResponseJsonOptions enrich) {
    utils::Json::Object obj;
    obj.emplace("page_count", static_cast<std::int64_t>(pages.size()));
    utils::Json::Array page_arr;
    utils::Json::Array combined;
    bool all_arrays = !pages.empty();
    for (const auto& p : pages) {
        auto j = response_to_json(p, enrich);
        if (j.contains("body") && j.at("body").is_array()) {
            for (const auto& item : j.at("body").as_array()) {
                combined.push_back(item);
            }
        } else {
            all_arrays = false;
        }
        page_arr.push_back(std::move(j));
    }
    obj.emplace("pages", utils::Json(std::move(page_arr)));
    if (all_arrays) {
        const auto count = static_cast<std::int64_t>(combined.size());
        obj.emplace("items", utils::Json(std::move(combined)));
        obj.emplace("item_count", count);
    }
    return utils::Json(std::move(obj));
}

utils::Json RestClient::resource_catalog_json() {
    utils::Json::Array resources;
    auto add = [&](const char* id, const char* summary, const char* requires_fields) {
        utils::Json::Object o;
        o.emplace("id", std::string(id));
        o.emplace("summary", std::string(summary));
        o.emplace("requires", std::string(requires_fields));
        resources.emplace_back(std::move(o));
    };
    add("rate_limit", "GET /rate_limit", "");
    add("user", "GET /user or /users/{username}", "username?");
    add("org", "GET /orgs/{org}", "org");
    add("repo", "GET /repos/{owner}/{repo}", "owner,repo");
    add("readme", "GET .../readme", "owner,repo,ref?");
    add("contents", "GET .../contents/{path}", "owner,repo,path?,ref?");
    add("raw", "GET contents with Accept raw", "owner,repo,path,ref?");
    add("git_tree", "GET .../git/trees/{sha}", "owner,repo,tree_sha,recursive?");
    add("git_blob", "GET .../git/blobs/{sha}", "owner,repo,file_sha");
    add("git_ref", "GET .../git/ref/{ref}", "owner,repo,git_ref");
    add("commit", "GET .../commits/{ref}", "owner,repo,ref|sha");
    add("commits", "GET .../commits", "owner,repo,+query");
    add("compare", "GET .../compare/{base}...{head}", "owner,repo,base,head");
    add("issue", "GET .../issues/{number}", "owner,repo,number");
    add("issues", "GET .../issues", "owner,repo,+query");
    add("issue_comments", "GET .../issues/{n}/comments", "owner,repo,number");
    add("pull", "GET .../pulls/{number}", "owner,repo,number");
    add("pulls", "GET .../pulls", "owner,repo,+query");
    add("pull_files", "GET .../pulls/{n}/files", "owner,repo,number");
    add("pull_commits", "GET .../pulls/{n}/commits", "owner,repo,number");
    add("release", "GET release by id or tag", "owner,repo,release_id|tag");
    add("latest_release", "GET .../releases/latest", "owner,repo");
    add("releases", "GET .../releases", "owner,repo");
    add("branches", "GET .../branches", "owner,repo");
    add("tags", "GET .../tags", "owner,repo");
    add("languages", "GET .../languages", "owner,repo");
    add("contributors", "GET .../contributors", "owner,repo");
    add("forks", "GET .../forks", "owner,repo");
    add("topics", "GET .../topics", "owner,repo");

    utils::Json::Object root;
    root.emplace("schema", 1);
    root.emplace("policy", std::string("full_fidelity"));
    root.emplace("note", std::string("Prefer github_resource for named reads; github_rest for arbitrary paths; "
                                     "github_rest_paginate to follow Link rel=next without dropping pages."));
    root.emplace("resources", utils::Json(std::move(resources)));
    return utils::Json(std::move(root));
}

} // namespace xscope::providers::github
