#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace xscope::xaiop {

class XaiopError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Thin loader around the embedded Go XAIOP native shared library.
class Bridge {
public:
    static Bridge& instance();

    std::string sdk_version();
    std::string protocol_version();

    /// STRICT parse wire → JSON text (materialized snapshot).
    std::string parse_to_json(const std::string& wire);

    /// JSON document/value → XAIOP wire (product encode defaults).
    std::string encode_json(const std::string& json);

    std::int64_t live_create();
    void live_free(std::int64_t id);
    void live_feed_text(std::int64_t id, const std::string& text);
    std::string live_snapshot_json(std::int64_t id);

private:
    Bridge();
    ~Bridge();
    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;

    void ensure_loaded();

    void* module_ = nullptr;
    using FreeFn = void (*)(char*);
    using CStringFn = char* (*)();
    using ParseFn = char* (*)(const char*, char**);
    using EncodeFn = char* (*)(const char*, char**);
    using LiveCreateFn = long long (*)();
    using LiveFreeFn = void (*)(long long);
    using LiveFeedFn = int (*)(long long, const char*, char**);
    using LiveSnapFn = char* (*)(long long, char**);

    FreeFn free_ = nullptr;
    CStringFn sdk_version_ = nullptr;
    CStringFn protocol_version_ = nullptr;
    ParseFn parse_to_json_ = nullptr;
    EncodeFn encode_json_ = nullptr;
    LiveCreateFn live_create_ = nullptr;
    LiveFreeFn live_free_ = nullptr;
    LiveFeedFn live_feed_ = nullptr;
    LiveSnapFn live_snap_ = nullptr;
};

} // namespace xscope::xaiop
