#pragma once

#include "xscope/auth/types.hpp"
#include "xscope/network/http_client.hpp"

#include <string>

namespace xscope::auth {

struct DeviceFlowEndpoints {
    /// e.g. https://github.com/login/device/code
    std::string device_code_url;
    /// e.g. https://github.com/login/oauth/access_token
    std::string token_url;
};

/// RFC 8628 device authorization grant over HTTP (provider-agnostic).
class DeviceFlowClient {
public:
    DeviceFlowClient(network::HttpClient& http, DeviceFlowEndpoints endpoints);

    DeviceAuthorization request_device_code(const std::string& client_id, const std::string& scope,
                                            const std::string& client_secret = {});

    PollResult poll_token(const std::string& client_id, const std::string& device_code,
                          const std::string& client_secret = {});

private:
    network::HttpClient& http_;
    DeviceFlowEndpoints endpoints_;
};

/// Best-effort open URL in the system browser (Windows: ShellExecute).
bool open_url_in_browser(const std::string& url);

} // namespace xscope::auth
