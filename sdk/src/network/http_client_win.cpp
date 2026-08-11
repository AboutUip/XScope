#include "xscope/network/http_client.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

namespace xscope::network {
namespace {

std::wstring widen_utf8(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) {
        throw NetworkError("failed to convert URL/header to UTF-16");
    }
    std::wstring out(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    return out;
}

std::string narrow_utf8(const std::wstring& s) {
    if (s.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

struct ParsedUrl {
    bool https = false;
    std::wstring host;
    INTERNET_PORT port = 0;
    std::wstring path_and_query;
};

ParsedUrl parse_url(const std::string& url) {
    const auto wurl = widen_utf8(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = static_cast<DWORD>(-1);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        throw NetworkError("invalid URL");
    }
    ParsedUrl out;
    out.https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    out.host.assign(uc.lpszHostName, uc.dwHostNameLength);
    out.port = uc.nPort;
    out.path_and_query.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength > 0 && uc.lpszExtraInfo) {
        out.path_and_query.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    }
    if (out.path_and_query.empty()) {
        out.path_and_query = L"/";
    }
    return out;
}

class WinHandles {
public:
    HINTERNET session = nullptr;
    HINTERNET connect = nullptr;
    HINTERNET request = nullptr;

    ~WinHandles() {
        if (request) {
            WinHttpCloseHandle(request);
        }
        if (connect) {
            WinHttpCloseHandle(connect);
        }
        if (session) {
            WinHttpCloseHandle(session);
        }
    }
};

void throw_winhttp(const char* what) {
    const DWORD err = GetLastError();
    std::ostringstream oss;
    oss << what << " (WinHTTP " << err << ")";
    throw NetworkError(oss.str());
}

void apply_timeouts(HINTERNET request, const HttpRequest& req) {
    const int resolve_ms = static_cast<int>(req.connect_timeout.count());
    const int connect_ms = static_cast<int>(req.connect_timeout.count());
    const int send_ms = static_cast<int>(req.timeout.count());
    const int recv_ms = static_cast<int>(req.timeout.count());
    WinHttpSetTimeouts(request, resolve_ms, connect_ms, send_ms, recv_ms);
}

void add_headers(HINTERNET request, const HeaderList& headers, const std::string& user_agent) {
    bool has_ua = false;
    for (const auto& [k, v] : headers) {
        if (_stricmp(k.c_str(), "User-Agent") == 0) {
            has_ua = true;
        }
        const auto line = widen_utf8(k + ": " + v);
        if (!WinHttpAddRequestHeaders(request, line.c_str(), static_cast<DWORD>(-1L),
                                      WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
            throw_winhttp("WinHttpAddRequestHeaders failed");
        }
    }
    if (!has_ua && !user_agent.empty()) {
        const auto line = widen_utf8(std::string("User-Agent: ") + user_agent);
        WinHttpAddRequestHeaders(request, line.c_str(), static_cast<DWORD>(-1L),
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }
}

HeaderList read_headers(HINTERNET request) {
    HeaderList out;
    DWORD size = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr,
                        &size, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) {
        return out;
    }
    std::wstring raw(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                             raw.data(), &size, WINHTTP_NO_HEADER_INDEX)) {
        return out;
    }
    const std::string text = narrow_utf8(raw);
    std::istringstream iss(text);
    std::string line;
    bool first = true;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (first) {
            first = false; // status line
            continue;
        }
        if (line.empty()) {
            continue;
        }
        const auto pos = line.find(':');
        if (pos == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
            value.erase(value.begin());
        }
        out.emplace_back(std::move(key), std::move(value));
    }
    return out;
}

int read_status(HINTERNET request) {
    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        throw_winhttp("query status failed");
    }
    return static_cast<int>(status);
}

HttpResponse perform(const ClientOptions& options, const HttpRequest& request, CancelToken* cancel,
                     const std::function<bool(std::span<const std::uint8_t>)>& on_chunk, bool aggregate_body) {
    if (cancel && cancel->is_cancelled()) {
        throw NetworkError("cancelled");
    }
    if (request.url.empty()) {
        throw NetworkError("url is empty");
    }

    const auto parsed = parse_url(request.url);
    WinHandles h;

    h.session = WinHttpOpen(widen_utf8(options.user_agent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!h.session) {
        throw_winhttp("WinHttpOpen failed");
    }

    h.connect = WinHttpConnect(h.session, parsed.host.c_str(), parsed.port, 0);
    if (!h.connect) {
        throw_winhttp("WinHttpConnect failed");
    }

    const auto method = widen_utf8(method_name(request.method));
    DWORD flags = parsed.https ? WINHTTP_FLAG_SECURE : 0;
    h.request = WinHttpOpenRequest(h.connect, method.c_str(), parsed.path_and_query.c_str(), nullptr,
                                   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!h.request) {
        throw_winhttp("WinHttpOpenRequest failed");
    }

    apply_timeouts(h.request, request);
    add_headers(h.request, request.headers, options.user_agent);

    const LPVOID body_ptr =
        request.body.empty() ? WINHTTP_NO_REQUEST_DATA : reinterpret_cast<LPVOID>(const_cast<char*>(request.body.data()));
    const DWORD body_len = static_cast<DWORD>(request.body.size());

    if (!WinHttpSendRequest(h.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, body_ptr, body_len, body_len, 0)) {
        if (cancel && cancel->is_cancelled()) {
            throw NetworkError("cancelled");
        }
        throw_winhttp("WinHttpSendRequest failed");
    }
    if (cancel && cancel->is_cancelled()) {
        throw NetworkError("cancelled");
    }
    if (!WinHttpReceiveResponse(h.request, nullptr)) {
        if (cancel && cancel->is_cancelled()) {
            throw NetworkError("cancelled");
        }
        throw_winhttp("WinHttpReceiveResponse failed");
    }

    HttpResponse response;
    response.status = read_status(h.request);
    response.headers = read_headers(h.request);

    for (;;) {
        if (cancel && cancel->is_cancelled()) {
            throw NetworkError("cancelled");
        }
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(h.request, &available)) {
            throw_winhttp("WinHttpQueryDataAvailable failed");
        }
        if (available == 0) {
            break;
        }
        std::vector<std::uint8_t> buf(available);
        DWORD read = 0;
        if (!WinHttpReadData(h.request, buf.data(), available, &read)) {
            throw_winhttp("WinHttpReadData failed");
        }
        if (read == 0) {
            break;
        }
        buf.resize(read);
        if (on_chunk) {
            if (!on_chunk(std::span<const std::uint8_t>(buf.data(), buf.size()))) {
                throw NetworkError("cancelled by stream consumer");
            }
        }
        if (aggregate_body) {
            response.body.append(reinterpret_cast<const char*>(buf.data()), buf.size());
        }
    }

    return response;
}

} // namespace

HttpClient::HttpClient(ClientOptions options) : options_(std::move(options)) {}

HttpResponse HttpClient::send(const HttpRequest& request, CancelToken* cancel) {
    return perform(options_, request, cancel, {}, true);
}

HttpResponse HttpClient::send_stream(const HttpRequest& request,
                                     const std::function<bool(std::span<const std::uint8_t>)>& on_chunk,
                                     CancelToken* cancel) {
    return perform(options_, request, cancel, on_chunk, false);
}

} // namespace xscope::network
