//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// End-to-end for the JS fetch shim: JS calls fetch() -> __twk_http -> the host
// delegate -> twk_http_respond -> the Promise resolves and JS reads the Response.
// Covers success, request shaping (method/headers/body), HTTP errors, transport
// failure, and AbortController timeouts. Returns 0 on pass.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "twk/twk.h"
#include "twk/twk_delegates.h"

using namespace std::chrono_literals;

namespace {

struct Request {
    twk_client* client;
    twk_token token;
    std::string method, url, headers, body;
};

std::mutex g_mutex;
std::vector<Request> g_requests;
std::atomic<int> g_cancels{0};
// When true the host never answers, so the JS-side timeout must fire.
std::atomic<bool> g_stall{false};

void on_http(void* /*user*/, twk_client* client, twk_token token, const char* method, const char* url,
             const char* headers_json, const char* body) {
    {
        std::lock_guard<std::mutex> guard(g_mutex);
        g_requests.push_back({client, token, method ? method : "", url ? url : "",
                              headers_json ? headers_json : "", body ? body : ""});
    }
    if (g_stall.load()) {
        return; // simulate a hung server
    }

    std::string u(url ? url : "");
    if (u.find("/fail") != std::string::npos) {
        twk_http_failed(client, token, "connection refused");
    } else if (u.find("/notfound") != std::string::npos) {
        twk_http_respond(client, token, 404, "{\"content-type\":\"application/json\"}", "{\"error\":\"nope\"}");
    } else {
        twk_http_respond(client, token, 200, "{\"Content-Type\":\"application/json; charset=utf-8\"}",
                         "{\"balance\":\"42\"}");
    }
}

void on_cancel(void* /*user*/, twk_client* /*client*/, twk_token /*token*/) {
    g_cancels.fetch_add(1);
}

bool contains(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

// Send one walletKit call and return its response JSON.
std::string call(twk_client* c, unsigned long long id, const char* method, const std::string& params) {
    twk_send(c, id, method, params.c_str());
    unsigned long long rid = 0;
    const char* out = twk_receive(c, 30.0, &rid);
    return out ? std::string(out) : std::string();
}

Request last_request() {
    std::lock_guard<std::mutex> guard(g_mutex);
    return g_requests.back();
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0); // unbuffered: a crash must not swallow progress
    printf("start\n");
    twk_delegates delegates{};
    delegates.http_request = on_http;
    delegates.http_cancel = on_cancel;
    twk_client* c = twk_client_create(&delegates, nullptr);

    bool ok = true;

    // 1. Happy path: JSON body and headers surface through the Response.
    std::string result = call(c, 1, "httpProbe", "[\"https://example.test/api/v3/getBalance?address=X\"]");
    bool got = contains(result, "\"ok\":true") && contains(result, "\"status\":200") &&
               contains(result, "\\\"balance\\\":\\\"42\\\"") && contains(result, "application/json");
    printf("%s: happy path -> %.120s\n", got ? "ok" : "FAIL", result.c_str());
    ok = ok && got;

    // Request shaping: method, URL and the headers the shim forwarded.
    Request req = last_request();
    bool shaped = req.method == "GET" && contains(req.url, "getBalance?address=X") &&
                  contains(req.headers, "x-probe");
    printf("%s: request shaping (method=%s headers=%s)\n", shaped ? "ok" : "FAIL", req.method.c_str(),
           req.headers.c_str());
    ok = ok && shaped;

    // 2. POST with a body.
    result = call(c, 2, "httpProbe",
                  "[\"https://example.test/api/v3/message\",{\"method\":\"POST\",\"body\":\"{\\\"boc\\\":\\\"x\\\"}\"}]");
    req = last_request();
    bool posted = req.method == "POST" && contains(req.body, "boc") && contains(req.headers, "content-type");
    printf("%s: POST body/headers (body=%s)\n", posted ? "ok" : "FAIL", req.body.c_str());
    ok = ok && posted;

    // 3. HTTP error status: still a resolved Response, ok=false.
    result = call(c, 3, "httpProbe", "[\"https://example.test/notfound\"]");
    got = contains(result, "\"ok\":false") && contains(result, "\"status\":404");
    printf("%s: 404 -> %.90s\n", got ? "ok" : "FAIL", result.c_str());
    ok = ok && got;

    // 4. Transport failure: fetch() rejects.
    result = call(c, 4, "httpProbe", "[\"https://example.test/fail\"]");
    got = contains(result, "\"failed\":true") && contains(result, "connection refused");
    printf("%s: transport failure -> %.110s\n", got ? "ok" : "FAIL", result.c_str());
    ok = ok && got;

    // 5. AbortController timeout: the host never answers, so the JS timeout fires,
    //    the promise rejects with AbortError, and the host sees a cancel.
    g_stall.store(true);
    int cancels_before = g_cancels.load();
    result = call(c, 5, "httpProbe", "[\"https://example.test/slow\",{\"timeoutMs\":80}]");
    got = contains(result, "\"failed\":true") && contains(result, "AbortError");
    bool cancelled = g_cancels.load() > cancels_before;
    printf("%s: abort/timeout (cancelled=%d) -> %.110s\n", got && cancelled ? "ok" : "FAIL", cancelled,
           result.c_str());
    ok = ok && got && cancelled;
    g_stall.store(false);

    twk_client_destroy(c);
    printf(ok ? "PASS\n" : "FAILED\n");
    return ok ? 0 : 1;
}
