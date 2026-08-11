#pragma once

#include <cstdint>
#include <filesystem>

namespace xscope::utils {

/// Unix epoch seconds (UTC).
std::int64_t now_unix_seconds();

/// Best-effort last-write-time as Unix epoch seconds.
std::int64_t file_mtime_unix_seconds(const std::filesystem::path& path);

} // namespace xscope::utils
