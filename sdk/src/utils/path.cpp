#include "xscope/utils/path.hpp"

#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace xscope::utils {

std::string path_to_utf8(const fs::path& path) {
    const auto u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

fs::path path_from_utf8(std::string_view utf8) {
    // MSVC: constructing path from std::string uses the narrow ACP, not UTF-8.
    // Always go through u8string so non-ASCII user folders (e.g. LocalAppData) work.
    std::u8string u8(utf8.begin(), utf8.end());
    return fs::path(u8);
}

void ensure_directory(const fs::path& dir) { fs::create_directories(dir); }

std::string read_file_utf8(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to read file: " + path_to_utf8(path));
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_file_utf8(const fs::path& path, std::string_view data) {
    ensure_directory(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("failed to write file: " + path_to_utf8(path));
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) {
        throw std::runtime_error("failed to write file: " + path_to_utf8(path));
    }
}

} // namespace xscope::utils
