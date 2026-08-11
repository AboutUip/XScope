#pragma once

#include <cstdint>
#include <string>

namespace xscope::xaiop {

/// Incremental XAIOP parser backed by the embedded Go LiveParser.
class LiveParser {
public:
    LiveParser();
    ~LiveParser();

    LiveParser(const LiveParser&) = delete;
    LiveParser& operator=(const LiveParser&) = delete;
    LiveParser(LiveParser&& other) noexcept;
    LiveParser& operator=(LiveParser&& other) noexcept;

    void feed_text(const std::string& text);
    std::string snapshot_json() const;

private:
    std::int64_t id_ = 0;
};

} // namespace xscope::xaiop
