#include "xscope/utils/time.hpp"

#include <chrono>

namespace fs = std::filesystem;

namespace xscope::utils {

std::int64_t now_unix_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::int64_t file_mtime_unix_seconds(const fs::path& path) {
    const auto ftime = fs::last_write_time(path);
    const auto now_file = fs::file_time_type::clock::now();
    const auto now_sys = std::chrono::system_clock::now();
    const auto sys = now_sys - (now_file - ftime);
    return std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
}

} // namespace xscope::utils
