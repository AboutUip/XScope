#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace xscope::utils {

std::string trim_copy(std::string_view s);
std::string trim_copy(std::string s);

/// Strip matching single/double quotes around a trimmed value.
std::string strip_quotes(std::string_view s);

/// Escape for embedding into a JSON string literal.
std::string json_escape(std::string_view s);

/// Keep [A-Za-z0-9._-], map spaces to '-'; trim leading '.' / '-'.
std::string sanitize_id(std::string_view id);

std::vector<std::string> split_lines(std::string_view text);

bool starts_with(std::string_view s, std::string_view prefix);
bool ends_with(std::string_view s, std::string_view suffix);

} // namespace xscope::utils
