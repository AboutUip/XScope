#pragma once

#include "xscope/network/http_client.hpp"

#include <functional>
#include <memory>
#include <string>

namespace xscope::network {

/// Optional XAIOP plane on top of generic HTTP.
/// Construct only when ClientOptions::enable_xaiop is true (or call ensure via NetworkClient).
class XaiopSession {
public:
    explicit XaiopSession(HttpClient& http);

    /// Encode JSON → XAIOP wire (uses embedded Go SDK).
    std::string encode_json(const std::string& json) const;

    /// Parse full XAIOP wire → JSON snapshot.
    std::string parse_to_json(const std::string& wire) const;

    /// POST XAIOP body. If `body_is_json`, encodes to wire first.
    HttpResponse post(const std::string& url, const std::string& body, bool body_is_json,
                      const HeaderList& extra_headers = {}, CancelToken* cancel = nullptr);

    /// GET and treat response as XAIOP. Invokes `on_phase` when a phase completes
    /// (line `.`) and once at end-of-stream with the latest snapshot JSON.
    HttpResponse get_stream(const std::string& url,
                            const std::function<void(const std::string& snapshot_json, bool is_final)>& on_phase,
                            const HeaderList& extra_headers = {}, CancelToken* cancel = nullptr);

    /// POST wire/json and stream XAIOP response phases (same callback contract as get_stream).
    HttpResponse post_stream(const std::string& url, const std::string& body, bool body_is_json,
                             const std::function<void(const std::string& snapshot_json, bool is_final)>& on_phase,
                             const HeaderList& extra_headers = {}, CancelToken* cancel = nullptr);

private:
    HttpRequest make_xaiop_request(HttpMethod method, const std::string& url, const std::string& wire,
                                   const HeaderList& extra) const;

    HttpResponse stream_xaiop_response(
        const HttpRequest& request,
        const std::function<void(const std::string& snapshot_json, bool is_final)>& on_phase,
        CancelToken* cancel);

    HttpClient& http_;
};

/// Facade: generic HTTP always; XAIOP helpers when enabled.
class NetworkClient {
public:
    explicit NetworkClient(ClientOptions options = {});

    HttpClient& http() noexcept { return http_; }
    const HttpClient& http() const noexcept { return http_; }
    const ClientOptions& options() const noexcept { return http_.options(); }

    bool xaiop_enabled() const noexcept { return http_.options().enable_xaiop; }

    /// Throws NetworkError if XAIOP was not enabled in options.
    XaiopSession& xaiop();

private:
    HttpClient http_;
    std::unique_ptr<XaiopSession> xaiop_;
};

} // namespace xscope::network
