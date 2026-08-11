#pragma once

#include "xscope/network/cancel.hpp"
#include "xscope/network/types.hpp"

#include <functional>
#include <span>

namespace xscope::network {

/// Generic HTTP transport. No business logic, no XAIOP parsing.
/// Intended dependency for MCP adapters, AI providers, and higher sessions.
class HttpClient {
public:
    explicit HttpClient(ClientOptions options = {});

    const ClientOptions& options() const noexcept { return options_; }

    HttpResponse send(const HttpRequest& request, CancelToken* cancel = nullptr);

    /// Stream response body. `on_chunk` return false to abort.
    /// Final `HttpResponse::body` is empty when streaming (chunks are not re-aggregated).
    HttpResponse send_stream(const HttpRequest& request,
                             const std::function<bool(std::span<const std::uint8_t>)>& on_chunk,
                             CancelToken* cancel = nullptr);

private:
    ClientOptions options_;
};

} // namespace xscope::network
