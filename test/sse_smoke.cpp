//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// SSE seam: JS EventSource -> __twk_sse_open -> the sse_open delegate, then many
// events and one close flowing back. Unlike HTTP this is multi-shot, so the test
// checks that the stream survives repeated events, carries event ids (TON Connect
// resumes from lastEventId), routes named event types, reports errors, and that
// close() stops delivery. Returns 0 on pass.
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

std::mutex g_mutex;
twk_client* g_client = nullptr;
twk_token g_token = 0;
std::atomic<bool> g_open{false};
std::atomic<int> g_closes{0};

void on_sse_open(void* /*user*/, twk_client* client, twk_token token, const char* /*url*/,
                 const char* /*headers*/) {
    std::lock_guard<std::mutex> guard(g_mutex);
    g_client = client;
    g_token = token;
    g_open.store(true);
}

void on_sse_close(void* /*user*/, twk_client* /*client*/, twk_token /*token*/) {
    g_closes.fetch_add(1);
    g_open.store(false);
}

void on_http(void* /*user*/, twk_client* client, twk_token token, const char* /*method*/, const char* /*url*/,
             const char* /*headers*/, const char* /*body*/) {
    twk_http_respond(client, token, 200, "{\"content-type\":\"application/json\"}", "{}");
}

// Push one SSE frame from the "host" side.
void push(const char* json) {
    std::lock_guard<std::mutex> guard(g_mutex);
    if (g_client != nullptr) {
        twk_sse_event(g_client, g_token, json);
    }
}

bool contains(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

struct Message {
    unsigned long long request_id;
    std::string json;
};

Message receive(twk_client* c, double timeout = 15.0) {
    unsigned long long rid = 0;
    const char* out = twk_receive(c, timeout, &rid);
    return {rid, out ? std::string(out) : std::string()};
}

// Drains messages until one is an event containing `needle` (responses ignored).
bool waitForEvent(twk_client* c, const char* needle, std::string* captured = nullptr) {
    for (int i = 0; i < 12; ++i) {
        Message m = receive(c);
        if (m.json.empty()) {
            return false;
        }
        if (m.request_id == 0 && contains(m.json, needle)) {
            if (captured != nullptr) {
                *captured = m.json;
            }
            return true;
        }
    }
    return false;
}

bool waitFor(std::atomic<bool>& flag, std::chrono::milliseconds limit = 5000ms) {
    auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (flag.load()) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return flag.load();
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);

    twk_delegates delegates{};
    delegates.http_request = on_http;
    delegates.sse_open = on_sse_open;
    delegates.sse_close = on_sse_close;
    twk_client* c = twk_client_create(&delegates, nullptr);

    bool ok = true;

    twk_send(c, 1, "initWalletKit", "[{\"networks\":[{\"chainId\":\"-3\"}]}]");
    receive(c);

    twk_send(c, 2, "sseProbe", "[\"https://bridge.test/events\"]");
    bool got = waitFor(g_open);
    printf("%s: EventSource reached the sse_open delegate\n", got ? "ok" : "FAIL");
    ok = ok && got;

    // First event also signals open.
    push("{\"data\":\"hello\",\"event\":\"message\",\"id\":\"1\"}");
    std::string captured;
    got = waitForEvent(c, "\"kind\":\"message\"", &captured) && contains(captured, "hello") &&
          contains(captured, "\"lastEventId\":\"1\"");
    printf("%s: message event + lastEventId -> %.110s\n", got ? "ok" : "FAIL", captured.c_str());
    ok = ok && got;

    // Multi-shot: the stream stays open for further events.
    push("{\"data\":\"second\",\"event\":\"message\",\"id\":\"2\"}");
    got = waitForEvent(c, "second");
    printf("%s: stream survives repeated events (multi-shot)\n", got ? "ok" : "FAIL");
    ok = ok && got;

    // Named event types route to addEventListener.
    push("{\"data\":\"payload\",\"event\":\"custom\",\"id\":\"3\"}");
    got = waitForEvent(c, "\"kind\":\"custom\"");
    printf("%s: named event type routed to addEventListener\n", got ? "ok" : "FAIL");
    ok = ok && got;

    // close() from JS reaches the host and stops delivery.
    int closes_before = g_closes.load();
    twk_send(c, 3, "sseProbeClose", "[]");
    std::this_thread::sleep_for(200ms);
    got = g_closes.load() > closes_before;
    printf("%s: close() reached the sse_close delegate\n", got ? "ok" : "FAIL");
    ok = ok && got;

    // A stream error surfaces as onerror.
    twk_send(c, 4, "sseProbe", "[\"https://bridge.test/events2\"]");
    g_open.store(false);
    waitFor(g_open);
    {
        std::lock_guard<std::mutex> guard(g_mutex);
        twk_sse_closed(g_client, g_token, "relay dropped");
    }
    got = waitForEvent(c, "\"kind\":\"error\"");
    printf("%s: stream error surfaced as onerror\n", got ? "ok" : "FAIL");
    ok = ok && got;

    twk_client_destroy(c);
    printf(ok ? "PASS\n" : "FAILED\n");
    return ok ? 0 : 1;
}
