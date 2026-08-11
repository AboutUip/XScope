#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xscope::utils {

/// Percent-encode for query / form values (RFC 3986 unreserved left as-is).
std::string url_encode(std::string_view s);

/// Build `k=v&k2=v2` with encoding. Empty values are still emitted as `k=`.
std::string build_query(const std::vector<std::pair<std::string, std::string>>& params);

/// Append query to a URL that may already contain `?`.
std::string url_with_query(std::string_view base_url,
                           const std::vector<std::pair<std::string, std::string>>& params);

} // namespace xscope::utils
