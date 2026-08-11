#include "xscope/xaiop/bridge.hpp"

#include "xscope/utils/json.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <filesystem>
#include <mutex>
#include <vector>

namespace fs = std::filesystem;

namespace xscope::xaiop {
namespace {

std::mutex g_mu;

// XAIOP wire forbids raw CR/LF inside string values. Map them to U+2028 so UI can restore.
std::string replace_all(std::string s, std::string_view from, std::string_view to) {
    if (from.empty()) {
        return s;
    }
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string utf8_line_sep() {
    // U+2028 as UTF-8: E2 80 A8
    return std::string("\xE2\x80\xA8");
}

std::string scrub_crlf_string(std::string s) {
    const auto sep = utf8_line_sep();
    s = replace_all(std::move(s), "\r\n", sep);
    s = replace_all(std::move(s), "\n", sep);
    s = replace_all(std::move(s), "\r", sep);
    return s;
}

std::string restore_line_sep_string(std::string s) {
    return replace_all(std::move(s), utf8_line_sep(), "\n");
}

utils::Json scrub_crlf_json(const utils::Json& in) {
    if (in.is_string()) {
        return utils::Json(scrub_crlf_string(in.as_string("")));
    }
    if (in.is_array()) {
        utils::Json::Array out;
        out.reserve(in.as_array().size());
        for (const auto& v : in.as_array()) {
            out.push_back(scrub_crlf_json(v));
        }
        return utils::Json(std::move(out));
    }
    if (in.is_object()) {
        utils::Json::Object out;
        for (const auto& [k, v] : in.as_object()) {
            out.emplace(scrub_crlf_string(k), scrub_crlf_json(v));
        }
        return utils::Json(std::move(out));
    }
    return in;
}

utils::Json restore_line_sep_json(const utils::Json& in) {
    if (in.is_string()) {
        return utils::Json(restore_line_sep_string(in.as_string("")));
    }
    if (in.is_array()) {
        utils::Json::Array out;
        out.reserve(in.as_array().size());
        for (const auto& v : in.as_array()) {
            out.push_back(restore_line_sep_json(v));
        }
        return utils::Json(std::move(out));
    }
    if (in.is_object()) {
        utils::Json::Object out;
        for (const auto& [k, v] : in.as_object()) {
            out.emplace(restore_line_sep_string(k), restore_line_sep_json(v));
        }
        return utils::Json(std::move(out));
    }
    return in;
}

fs::path module_dir() {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&module_dir), &self)) {
        return fs::current_path();
    }
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(self, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return fs::current_path();
    }
    return fs::path(buf).parent_path();
}

fs::path find_dll() {
    const wchar_t* name = L"xaiop_native.dll";
    std::vector<fs::path> candidates = {
        module_dir() / name,
        fs::current_path() / name,
        module_dir() / "deps" / "xaiop_native" / "build" / name,
    };
    for (const auto& p : candidates) {
        if (fs::exists(p)) {
            return p;
        }
    }
    return module_dir() / name;
}

std::string take_c_string(char* p, void (*free_fn)(char*)) {
    if (!p) {
        return {};
    }
    std::string s(p);
    free_fn(p);
    return s;
}

} // namespace

Bridge& Bridge::instance() {
    static Bridge bridge;
    return bridge;
}

Bridge::Bridge() = default;

Bridge::~Bridge() {
    if (module_) {
        FreeLibrary(static_cast<HMODULE>(module_));
        module_ = nullptr;
    }
}

void Bridge::ensure_loaded() {
    std::scoped_lock lock(g_mu);
    if (module_) {
        return;
    }
    const auto path = find_dll();
    HMODULE mod = LoadLibraryW(path.c_str());
    if (!mod) {
        throw XaiopError("failed to load xaiop_native.dll from " + path.string());
    }
    module_ = mod;

    free_ = reinterpret_cast<FreeFn>(GetProcAddress(mod, "xaiop_native_free"));
    sdk_version_ = reinterpret_cast<CStringFn>(GetProcAddress(mod, "xaiop_native_sdk_version"));
    protocol_version_ =
        reinterpret_cast<CStringFn>(GetProcAddress(mod, "xaiop_native_protocol_version"));
    parse_to_json_ = reinterpret_cast<ParseFn>(GetProcAddress(mod, "xaiop_native_parse_to_json"));
    encode_json_ = reinterpret_cast<EncodeFn>(GetProcAddress(mod, "xaiop_native_encode_json"));
    live_create_ = reinterpret_cast<LiveCreateFn>(GetProcAddress(mod, "xaiop_native_live_create"));
    live_free_ = reinterpret_cast<LiveFreeFn>(GetProcAddress(mod, "xaiop_native_live_free"));
    live_feed_ = reinterpret_cast<LiveFeedFn>(GetProcAddress(mod, "xaiop_native_live_feed_text"));
    live_snap_ = reinterpret_cast<LiveSnapFn>(GetProcAddress(mod, "xaiop_native_live_snapshot_json"));

    if (!free_ || !sdk_version_ || !protocol_version_ || !parse_to_json_ || !encode_json_ ||
        !live_create_ || !live_free_ || !live_feed_ || !live_snap_) {
        FreeLibrary(mod);
        module_ = nullptr;
        throw XaiopError("xaiop_native.dll is missing required exports");
    }
}

std::string Bridge::sdk_version() {
    ensure_loaded();
    return take_c_string(sdk_version_(), free_);
}

std::string Bridge::protocol_version() {
    ensure_loaded();
    return take_c_string(protocol_version_(), free_);
}

std::string Bridge::parse_to_json(const std::string& wire) {
    ensure_loaded();
    char* err = nullptr;
    char* out = parse_to_json_(wire.c_str(), &err);
    if (!out) {
        std::string msg = err ? take_c_string(err, free_) : "parse failed";
        throw XaiopError(msg);
    }
    auto json_text = take_c_string(out, free_);
    try {
        auto doc = utils::Json::parse(json_text);
        return restore_line_sep_json(doc).dump(0);
    } catch (...) {
        return json_text;
    }
}

std::string Bridge::encode_json(const std::string& json) {
    ensure_loaded();
    std::string scrubbed = json;
    try {
        scrubbed = scrub_crlf_json(utils::Json::parse(json)).dump(0);
    } catch (...) {
        // If input is not JSON, still strip raw CR/LF as a last resort.
        scrubbed = scrub_crlf_string(json);
    }
    char* err = nullptr;
    char* out = encode_json_(scrubbed.c_str(), &err);
    if (!out) {
        std::string msg = err ? take_c_string(err, free_) : "encode failed";
        throw XaiopError(msg);
    }
    return take_c_string(out, free_);
}

std::int64_t Bridge::live_create() {
    ensure_loaded();
    return static_cast<std::int64_t>(live_create_());
}

void Bridge::live_free(std::int64_t id) {
    ensure_loaded();
    live_free_(static_cast<long long>(id));
}

void Bridge::live_feed_text(std::int64_t id, const std::string& text) {
    ensure_loaded();
    char* err = nullptr;
    if (!live_feed_(static_cast<long long>(id), text.c_str(), &err)) {
        std::string msg = err ? take_c_string(err, free_) : "live feed failed";
        throw XaiopError(msg);
    }
}

std::string Bridge::live_snapshot_json(std::int64_t id) {
    ensure_loaded();
    char* err = nullptr;
    char* out = live_snap_(static_cast<long long>(id), &err);
    if (!out) {
        std::string msg = err ? take_c_string(err, free_) : "live snapshot failed";
        throw XaiopError(msg);
    }
    return take_c_string(out, free_);
}

} // namespace xscope::xaiop
