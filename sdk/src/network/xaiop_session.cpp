#include "xscope/network/xaiop_session.hpp"

#include "xscope/xaiop/bridge.hpp"
#include "xscope/xaiop/live_parser.hpp"

#include <span>

namespace xscope::network {
namespace {

class LineAssembler {
public:
    /// Feed raw bytes; invoke on_line for each complete logical line (without LF).
    template <typename Fn>
    void feed(std::span<const std::uint8_t> chunk, Fn&& on_line) {
        buf_.append(reinterpret_cast<const char*>(chunk.data()), chunk.size());
        for (;;) {
            const auto pos = buf_.find('\n');
            if (pos == std::string::npos) {
                break;
            }
            std::string line = buf_.substr(0, pos);
            buf_.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            on_line(line);
        }
    }

    void flush_remainder(const std::function<void(const std::string&)>& on_line) {
        if (buf_.empty()) {
            return;
        }
        std::string line = std::move(buf_);
        buf_.clear();
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            on_line(line);
        }
    }

private:
    std::string buf_;
};

} // namespace

XaiopSession::XaiopSession(HttpClient& http) : http_(http) {}

std::string XaiopSession::encode_json(const std::string& json) const {
    return xscope::xaiop::Bridge::instance().encode_json(json);
}

std::string XaiopSession::parse_to_json(const std::string& wire) const {
    return xscope::xaiop::Bridge::instance().parse_to_json(wire);
}

HttpRequest XaiopSession::make_xaiop_request(HttpMethod method, const std::string& url,
                                            const std::string& wire, const HeaderList& extra) const {
    HttpRequest req;
    req.method = method;
    req.url = url;
    req.body = wire;
    req.headers = extra;
    req.headers.emplace_back("Content-Type", http_.options().xaiop_content_type);
    req.headers.emplace_back("Accept", http_.options().xaiop_accept);
    return req;
}

HttpResponse XaiopSession::post(const std::string& url, const std::string& body, bool body_is_json,
                                const HeaderList& extra_headers, CancelToken* cancel) {
    const std::string wire = body_is_json ? encode_json(body) : body;
    return http_.send(make_xaiop_request(HttpMethod::Post, url, wire, extra_headers), cancel);
}

HttpResponse XaiopSession::stream_xaiop_response(
    const HttpRequest& request,
    const std::function<void(const std::string& snapshot_json, bool is_final)>& on_phase,
    CancelToken* cancel) {
    xscope::xaiop::LiveParser live;
    LineAssembler lines;

    auto handle_line = [&](const std::string& line) {
        live.feed_text(line + "\n");
        if (line == "." && on_phase) {
            on_phase(live.snapshot_json(), false);
        }
    };

    auto response = http_.send_stream(
        request,
        [&](std::span<const std::uint8_t> chunk) {
            if (cancel && cancel->is_cancelled()) {
                return false;
            }
            lines.feed(chunk, handle_line);
            return true;
        },
        cancel);

    lines.flush_remainder(handle_line);
    if (on_phase) {
        on_phase(live.snapshot_json(), true);
    }
    return response;
}

HttpResponse XaiopSession::get_stream(
    const std::string& url,
    const std::function<void(const std::string& snapshot_json, bool is_final)>& on_phase,
    const HeaderList& extra_headers, CancelToken* cancel) {
    auto req = make_xaiop_request(HttpMethod::Get, url, {}, extra_headers);
    req.body.clear();
    // GET should not force Content-Type with empty body; keep Accept.
    req.headers.clear();
    req.headers = extra_headers;
    req.headers.emplace_back("Accept", http_.options().xaiop_accept);
    return stream_xaiop_response(req, on_phase, cancel);
}

HttpResponse XaiopSession::post_stream(
    const std::string& url, const std::string& body, bool body_is_json,
    const std::function<void(const std::string& snapshot_json, bool is_final)>& on_phase,
    const HeaderList& extra_headers, CancelToken* cancel) {
    const std::string wire = body_is_json ? encode_json(body) : body;
    return stream_xaiop_response(make_xaiop_request(HttpMethod::Post, url, wire, extra_headers),
                                 on_phase, cancel);
}

NetworkClient::NetworkClient(ClientOptions options) : http_(std::move(options)) {
    if (http_.options().enable_xaiop) {
        xaiop_ = std::make_unique<XaiopSession>(http_);
    }
}

XaiopSession& NetworkClient::xaiop() {
    if (!xaiop_) {
        throw NetworkError("XAIOP is not enabled; set ClientOptions::enable_xaiop = true");
    }
    return *xaiop_;
}

} // namespace xscope::network
