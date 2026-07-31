//
// Reference host — test-only twk delegate implementation (see reference_host.h).
//
#include "reference_host.h"

#include <atomic>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <winhttp.h>
#endif

namespace twk {
namespace refhost {

namespace {

// ---- minimal JSON for the flat string maps the delegate ABI uses ----------

std::string json_escape(const std::string& in) {
    std::string out;
    for (char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

std::string to_json(const std::map<std::string, std::string>& map) {
    std::string out = "{";
    bool first = true;
    for (const auto& [key, value] : map) {
        if (!first) {
            out += ',';
        }
        first = false;
        out += '"' + json_escape(key) + "\":\"" + json_escape(value) + '"';
    }
    return out + '}';
}

// Parses {"a":"b",...} — the shape our fetch shim emits. Not a general parser.
std::map<std::string, std::string> from_json(const std::string& in) {
    std::map<std::string, std::string> out;
    size_t i = 0;
    auto read_string = [&](std::string& value) -> bool {
        while (i < in.size() && in[i] != '"') {
            ++i;
        }
        if (i >= in.size()) {
            return false;
        }
        ++i; // opening quote
        value.clear();
        while (i < in.size() && in[i] != '"') {
            if (in[i] == '\\' && i + 1 < in.size()) {
                ++i;
                switch (in[i]) {
                    case 'n': value += '\n'; break;
                    case 'r': value += '\r'; break;
                    case 't': value += '\t'; break;
                    default: value += in[i];
                }
            } else {
                value += in[i];
            }
            ++i;
        }
        ++i; // closing quote
        return true;
    };

    while (i < in.size()) {
        std::string key, value;
        if (!read_string(key)) {
            break;
        }
        while (i < in.size() && in[i] != ':') {
            ++i;
        }
        if (i >= in.size() || !read_string(value)) {
            break;
        }
        out[key] = value;
    }
    return out;
}

#if defined(_WIN32)

std::wstring widen(const std::string& in) {
    if (in.empty()) {
        return {};
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, in.c_str(), static_cast<int>(in.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, in.c_str(), static_cast<int>(in.size()), &out[0], len);
    return out;
}

std::string narrow(const std::wstring& in) {
    if (in.empty()) {
        return {};
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, in.c_str(), static_cast<int>(in.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, in.c_str(), static_cast<int>(in.size()), &out[0], len, nullptr, nullptr);
    return out;
}

struct HttpOutcome {
    bool ok = false;
    int status = 0;
    std::string headers_json;
    std::string body;
    std::string error;
};

// Synchronous WinHTTP request; runs on a worker thread owned by the host.
HttpOutcome do_request(const std::string& method, const std::string& url, const std::string& headers_json,
                       const std::string& body) {
    HttpOutcome outcome;

    std::wstring wurl = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {0};
    wchar_t path[4096] = {0};
    wchar_t extra[4096] = {0};
    parts.lpszHostName = host;
    parts.dwHostNameLength = ARRAYSIZE(host);
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = ARRAYSIZE(path);
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = ARRAYSIZE(extra);

    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) {
        outcome.error = "invalid url";
        return outcome;
    }

    HINTERNET session = WinHttpOpen(L"ton-walletkit-core-refhost/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        outcome.error = "WinHttpOpen failed";
        return outcome;
    }

    HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
    if (connection == nullptr) {
        WinHttpCloseHandle(session);
        outcome.error = "WinHttpConnect failed";
        return outcome;
    }

    std::wstring target = std::wstring(path) + extra;
    DWORD flags = (parts.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connection, widen(method).c_str(), target.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (request == nullptr) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        outcome.error = "WinHttpOpenRequest failed";
        return outcome;
    }

    std::wstring header_block;
    for (const auto& [key, value] : from_json(headers_json)) {
        header_block += widen(key) + L": " + widen(value) + L"\r\n";
    }

    BOOL sent = WinHttpSendRequest(request, header_block.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : header_block.c_str(),
                                   header_block.empty() ? 0 : static_cast<DWORD>(-1),
                                   body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
                                   static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
        outcome.error = "request failed (" + std::to_string(GetLastError()) + ")";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return outcome;
    }

    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    outcome.status = static_cast<int>(status);

    // Response headers -> flat JSON (the shim only needs content-type).
    DWORD header_size = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &header_size,
                        WINHTTP_NO_HEADER_INDEX);
    std::map<std::string, std::string> response_headers;
    if (header_size > 0) {
        std::wstring raw(header_size / sizeof(wchar_t), L'\0');
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, &raw[0],
                                &header_size, WINHTTP_NO_HEADER_INDEX)) {
            std::string all = narrow(raw);
            size_t pos = 0;
            while (pos < all.size()) {
                size_t end = all.find("\r\n", pos);
                if (end == std::string::npos) {
                    end = all.size();
                }
                std::string line = all.substr(pos, end - pos);
                size_t colon = line.find(':');
                if (colon != std::string::npos) {
                    std::string key = line.substr(0, colon);
                    std::string value = line.substr(colon + 1);
                    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                        value.erase(value.begin());
                    }
                    for (char& c : key) {
                        c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
                    }
                    response_headers[key] = value;
                }
                pos = end + 2;
            }
        }
    }
    outcome.headers_json = to_json(response_headers);

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
            break;
        }
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, &chunk[0], available, &read)) {
            break;
        }
        outcome.body.append(chunk.data(), read);
    }

    outcome.ok = true;
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return outcome;
}

#endif // _WIN32

} // namespace

// Owns the worker threads and cancellation flags for in-flight requests.
struct ReferenceHost::Impl {
    std::mutex mutex;
    std::vector<std::thread> threads;
    std::unordered_map<int64_t, bool> cancelled;

    ~Impl() {
        // Requests may still be running; join so no thread outlives the host.
        std::vector<std::thread> pending;
        {
            std::lock_guard<std::mutex> guard(mutex);
            pending.swap(threads);
        }
        for (auto& t : pending) {
            if (t.joinable()) {
                t.join();
            }
        }
    }

    void cancel(int64_t token) {
        std::lock_guard<std::mutex> guard(mutex);
        cancelled[token] = true;
    }

    bool isCancelled(int64_t token) {
        std::lock_guard<std::mutex> guard(mutex);
        auto it = cancelled.find(token);
        return it != cancelled.end() && it->second;
    }

    void forget(int64_t token) {
        std::lock_guard<std::mutex> guard(mutex);
        cancelled.erase(token);
    }

    void spawn(std::thread t) {
        std::lock_guard<std::mutex> guard(mutex);
        threads.push_back(std::move(t));
    }
};

bool ReferenceHost::httpAvailable() {
#if defined(_WIN32)
    return true;
#else
    return false; // libcurl backend not implemented yet
#endif
}

namespace {

void on_http_request(void* user, twk_client* client, twk_token token, const char* method, const char* url,
                     const char* headers_json, const char* body) {
    auto* impl = static_cast<ReferenceHost::Impl*>(user);

#if defined(_WIN32)
    impl->spawn(std::thread([impl, client, token, m = std::string(method ? method : "GET"),
                             u = std::string(url ? url : ""), h = std::string(headers_json ? headers_json : "{}"),
                             b = std::string(body ? body : "")] {
        HttpOutcome outcome = do_request(m, u, h, b);

        // A cancelled request is simply dropped; the core already rejected it and
        // ignores late completions anyway.
        if (impl->isCancelled(token)) {
            impl->forget(token);
            return;
        }
        impl->forget(token);

        if (outcome.ok) {
            twk_http_respond(client, token, outcome.status, outcome.headers_json.c_str(), outcome.body.c_str());
        } else {
            twk_http_failed(client, token, outcome.error.c_str());
        }
    }));
#else
    (void)impl;
    (void)method;
    (void)url;
    (void)headers_json;
    (void)body;
    twk_http_failed(client, token, "reference host: no HTTP backend on this platform");
#endif
}

void on_http_cancel(void* user, twk_client* /*client*/, twk_token token) {
    static_cast<ReferenceHost::Impl*>(user)->cancel(token);
}

} // namespace

ReferenceHost::ReferenceHost() : impl_(std::make_unique<Impl>()) {
    delegates_.http_request = on_http_request;
    delegates_.http_cancel = on_http_cancel;
}

ReferenceHost::~ReferenceHost() = default;

void* ReferenceHost::userData() {
    return impl_.get();
}

} // namespace refhost
} // namespace twk
