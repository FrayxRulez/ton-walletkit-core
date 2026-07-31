//
// Reference host — test-only twk delegate implementation (see reference_host.h).
//
#include "reference_host.h"

#include "sse_parser.h"

#include <atomic>
#include <cstdio>
#include <functional>
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

// --- SSE (text/event-stream) ----------------------------------------------

struct SseStream {
    std::atomic<bool> cancelled{false};
};

// Runs a stream to completion on its own thread.
void run_sse(twk_client* client, twk_token token, const std::string& url, const std::string& headers_json,
             const std::shared_ptr<SseStream>& stream) {
    auto fail = [&](const char* message) { twk_sse_closed(client, token, message); };

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
        fail("invalid url");
        return;
    }

    HINTERNET session = WinHttpOpen(L"ton-walletkit-core-refhost/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        fail("WinHttpOpen failed");
        return;
    }
    // A finite receive timeout is what makes cancellation safe: the read wakes up
    // periodically so the cancel flag can be checked, instead of closing the
    // handle from another thread while a read is in flight.
    WinHttpSetTimeouts(session, 15000, 15000, 30000, 1000);

    HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
    if (connection == nullptr) {
        WinHttpCloseHandle(session);
        fail("WinHttpConnect failed");
        return;
    }

    std::wstring target = std::wstring(path) + extra;
    DWORD flags = (parts.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (request == nullptr) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        fail("WinHttpOpenRequest failed");
        return;
    }

    std::wstring header_block = L"Accept: text/event-stream\r\nCache-Control: no-cache\r\n";
    for (const auto& [key, value] : from_json(headers_json)) {
        header_block += widen(key) + L": " + widen(value) + L"\r\n";
    }

    if (!WinHttpSendRequest(request, header_block.c_str(), static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0,
                            0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        fail("SSE connect failed");
        return;
    }

    SseParser parser([&](const std::string& data, const std::string& event, const std::string& id) {
        std::map<std::string, std::string> frame{{"data", data}, {"event", event}, {"id", id}};
        twk_sse_event(client, token, to_json(frame).c_str());
    });

    std::string error;
    while (!stream->cancelled.load()) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            if (GetLastError() == ERROR_WINHTTP_TIMEOUT) {
                continue; // idle stream: loop so the cancel flag is re-checked
            }
            error = "stream read failed";
            break;
        }
        if (available == 0) {
            error.clear(); // server closed the stream cleanly
            break;
        }

        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, &chunk[0], available, &read)) {
            if (GetLastError() == ERROR_WINHTTP_TIMEOUT) {
                continue;
            }
            error = "stream read failed";
            break;
        }
        parser.feed(chunk.data(), read);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    // A cancelled stream was closed by the app; the core already dropped it.
    if (!stream->cancelled.load()) {
        twk_sse_closed(client, token, error.empty() ? nullptr : error.c_str());
    }
}

#endif // _WIN32

} // namespace

// Owns the worker threads, cancellation flags, and the key/value store.
struct ReferenceHost::Impl {
    std::mutex mutex;
    std::vector<std::thread> threads;
    std::unordered_map<int64_t, bool> cancelled;

    // Storage. Persisted to `storage_file` when set, so data survives a client
    // (and the process), as PasswordVault/Keychain would.
    std::string storage_file;
    std::mutex storage_mutex;
    std::map<std::string, std::string> store;

    void loadStore() {
        if (storage_file.empty()) {
            return;
        }
        FILE* f = std::fopen(storage_file.c_str(), "rb");
        if (f == nullptr) {
            return;
        }
        std::string contents;
        char buffer[4096];
        size_t got;
        while ((got = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
            contents.append(buffer, got);
        }
        std::fclose(f);
        store = from_json(contents);
    }

    void saveStore() {
        if (storage_file.empty()) {
            return;
        }
        std::string json = to_json(store);
        FILE* f = std::fopen(storage_file.c_str(), "wb");
        if (f == nullptr) {
            return;
        }
        std::fwrite(json.data(), 1, json.size(), f);
        std::fclose(f);
    }

    ~Impl() {
#if defined(_WIN32)
        // Streams block on reads, so tell them to stop before joining.
        cancelAllStreams();
#endif
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

#if defined(_WIN32)
    // Live SSE streams, so close() can signal the reader thread.
    std::mutex sse_mutex;
    std::map<int64_t, std::shared_ptr<SseStream>> sse_streams;

    std::shared_ptr<SseStream> addStream(int64_t token) {
        auto stream = std::make_shared<SseStream>();
        std::lock_guard<std::mutex> guard(sse_mutex);
        sse_streams[token] = stream;
        return stream;
    }

    void cancelStream(int64_t token) {
        std::shared_ptr<SseStream> stream;
        {
            std::lock_guard<std::mutex> guard(sse_mutex);
            auto it = sse_streams.find(token);
            if (it == sse_streams.end()) {
                return;
            }
            stream = it->second;
            sse_streams.erase(it);
        }
        stream->cancelled.store(true); // the reader notices on its next timeout
    }

    void cancelAllStreams() {
        std::map<int64_t, std::shared_ptr<SseStream>> streams;
        {
            std::lock_guard<std::mutex> guard(sse_mutex);
            streams.swap(sse_streams);
        }
        for (auto& [token, stream] : streams) {
            stream->cancelled.store(true);
        }
    }
#endif
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

void on_sse_open(void* user, twk_client* client, twk_token token, const char* url, const char* headers_json) {
    auto* impl = static_cast<ReferenceHost::Impl*>(user);
#if defined(_WIN32)
    auto stream = impl->addStream(token);
    impl->spawn(std::thread([client, token, u = std::string(url ? url : ""),
                             h = std::string(headers_json ? headers_json : "{}"), stream] {
        run_sse(client, token, u, h, stream);
    }));
#else
    (void)impl;
    (void)url;
    (void)headers_json;
    twk_sse_closed(client, token, "reference host: no SSE backend on this platform");
#endif
}

void on_sse_close(void* user, twk_client* /*client*/, twk_token token) {
#if defined(_WIN32)
    static_cast<ReferenceHost::Impl*>(user)->cancelStream(token);
#else
    (void)user;
    (void)token;
#endif
}

// Storage. Completed synchronously — the delegate contract allows it, and it
// keeps the reference host simple; a real host (PasswordVault) may go async.
void on_storage_get(void* user, twk_client* client, twk_token token, const char* key) {
    auto* impl = static_cast<ReferenceHost::Impl*>(user);
    std::lock_guard<std::mutex> guard(impl->storage_mutex);
    auto it = impl->store.find(key ? key : "");
    twk_storage_respond(client, token, it == impl->store.end() ? nullptr : it->second.c_str());
}

void on_storage_set(void* user, twk_client* client, twk_token token, const char* key, const char* value) {
    auto* impl = static_cast<ReferenceHost::Impl*>(user);
    {
        std::lock_guard<std::mutex> guard(impl->storage_mutex);
        impl->store[key ? key : ""] = value ? value : "";
        impl->saveStore();
    }
    twk_storage_respond(client, token, "");
}

void on_storage_remove(void* user, twk_client* client, twk_token token, const char* key) {
    auto* impl = static_cast<ReferenceHost::Impl*>(user);
    {
        std::lock_guard<std::mutex> guard(impl->storage_mutex);
        impl->store.erase(key ? key : "");
        impl->saveStore();
    }
    twk_storage_respond(client, token, "");
}

void on_storage_clear(void* user, twk_client* client, twk_token token) {
    auto* impl = static_cast<ReferenceHost::Impl*>(user);
    {
        std::lock_guard<std::mutex> guard(impl->storage_mutex);
        impl->store.clear();
        impl->saveStore();
    }
    twk_storage_respond(client, token, "");
}

} // namespace

ReferenceHost::ReferenceHost(std::string storage_file) : impl_(std::make_unique<Impl>()) {
    impl_->storage_file = std::move(storage_file);
    impl_->loadStore();

    delegates_.http_request = on_http_request;
    delegates_.http_cancel = on_http_cancel;
    delegates_.sse_open = on_sse_open;
    delegates_.sse_close = on_sse_close;
    delegates_.storage_get = on_storage_get;
    delegates_.storage_set = on_storage_set;
    delegates_.storage_remove = on_storage_remove;
    delegates_.storage_clear = on_storage_clear;
}

ReferenceHost::~ReferenceHost() = default;

void* ReferenceHost::userData() {
    return impl_.get();
}

} // namespace refhost
} // namespace twk
