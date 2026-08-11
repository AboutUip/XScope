#pragma once

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace xscope::network {

class NetworkError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class HttpMethod { Get, Post, Put, Patch, Delete, Head };

using HeaderList = std::vector<std::pair<std::string, std::string>>;

struct HttpRequest {
    HttpMethod method = HttpMethod::Get;
    std::string url;
    HeaderList headers;
    std::string body;
    std::chrono::milliseconds timeout{std::chrono::seconds(30)};
    std::chrono::milliseconds connect_timeout{std::chrono::seconds(15)};
};

struct HttpResponse {
    int status = 0;
    HeaderList headers;
    std::string body;
};

struct ClientOptions {
    /// When true, XAIOP helpers are available (MCP / AI / UI streams).
    /// Raw HTTP always works regardless of this flag.
    bool enable_xaiop = false;

    std::string user_agent = "XScope-Network/0.1";
    std::string xaiop_content_type = "application/x-xaiop; charset=utf-8";
    std::string xaiop_accept = "application/x-xaiop, application/json;q=0.8, */*;q=0.1";
};

const char* method_name(HttpMethod method);

} // namespace xscope::network
