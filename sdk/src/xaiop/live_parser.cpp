#include "xscope/xaiop/live_parser.hpp"

#include "xscope/xaiop/bridge.hpp"

namespace xscope::xaiop {

LiveParser::LiveParser() : id_(Bridge::instance().live_create()) {}

LiveParser::~LiveParser() {
    if (id_ != 0) {
        Bridge::instance().live_free(id_);
        id_ = 0;
    }
}

LiveParser::LiveParser(LiveParser&& other) noexcept : id_(other.id_) { other.id_ = 0; }

LiveParser& LiveParser::operator=(LiveParser&& other) noexcept {
    if (this != &other) {
        if (id_ != 0) {
            Bridge::instance().live_free(id_);
        }
        id_ = other.id_;
        other.id_ = 0;
    }
    return *this;
}

void LiveParser::feed_text(const std::string& text) { Bridge::instance().live_feed_text(id_, text); }

std::string LiveParser::snapshot_json() const { return Bridge::instance().live_snapshot_json(id_); }

} // namespace xscope::xaiop
