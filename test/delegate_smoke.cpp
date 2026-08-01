//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// Exercises the host-delegate seam: the core calls http_request, the host
// completes from another thread, and the result lands back on the worker thread.
// Also covers cancellation, transport failure, missing-delegate, and completions
// racing teardown. Returns 0 on pass.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include "client.h"
#include "twk/twk.h"
#include "twk/twk_delegates.h"

using namespace std::chrono_literals;

namespace {

// A fake host: records requests and completes them on a separate thread.
struct FakeHost {
    struct Call {
        twk_client* client;
        twk_token token;
        std::string method, url, headers, body;
    };

    std::mutex mutex;
    std::vector<Call> calls;
    std::atomic<int> cancels{0};
    std::vector<std::thread> threads;

    ~FakeHost() {
        for (auto& t : threads) {
            if (t.joinable()) {
                t.join();
            }
        }
    }

    void record(twk_client* c, twk_token token, const char* method, const char* url, const char* headers,
                const char* body) {
        std::lock_guard<std::mutex> g(mutex);
        calls.push_back({c, token, method ? method : "", url ? url : "", headers ? headers : "",
                         body ? body : ""});
    }

    size_t count() {
        std::lock_guard<std::mutex> g(mutex);
        return calls.size();
    }

    Call at(size_t i) {
        std::lock_guard<std::mutex> g(mutex);
        return calls[i];
    }
};

FakeHost* g_host = nullptr;

void on_http(void* /*user*/, twk_client* client, twk_token token, const char* method, const char* url,
             const char* headers_json, const char* body) {
    g_host->record(client, token, method, url, headers_json, body);
}

void on_cancel(void* /*user*/, twk_client* /*client*/, twk_token /*token*/) {
    g_host->cancels.fetch_add(1);
}

twk_delegates make_delegates() {
    twk_delegates d{};
    d.http_request = on_http;
    d.http_cancel = on_cancel;
    return d;
}

// Wait for a condition, up to `limit`. Returns false on timeout.
template <typename F>
bool wait_for(F cond, std::chrono::milliseconds limit = 3000ms) {
    auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (cond()) {
            return true;
        }
        std::this_thread::sleep_for(2ms);
    }
    return cond();
}

// The core side: start a request through Client and capture the result.
twk::HttpResult run_request(twk::Client& client, std::promise<twk::HttpResult>& done, int64_t* out_token) {
    auto fut = done.get_future();
    *out_token = client.startHttp("GET", "https://example.test/api/v3/x?y=1", "{\"A\":\"b\"}", "",
                                  [&done](twk::HttpResult r) { done.set_value(std::move(r)); });
    return fut.get();
}

bool test_round_trip() {
    FakeHost host;
    g_host = &host;
    auto delegates = make_delegates();
    auto* client = new twk::Client(&delegates, nullptr);

    std::promise<twk::HttpResult> done;
    auto fut = done.get_future();
    int64_t token = client->startHttp("GET", "https://example.test/api/v3/x?y=1", "{\"A\":\"b\"}", "",
                                      [&done](twk::HttpResult r) { done.set_value(std::move(r)); });

    if (token == 0 || !wait_for([&] { return host.count() == 1; })) {
        printf("round_trip: delegate not called\n");
        delete client;
        return false;
    }

    auto call = host.at(0);
    bool args_ok = call.method == "GET" && call.url == "https://example.test/api/v3/x?y=1" &&
                   call.headers == "{\"A\":\"b\"}" && call.token == token;

    // Complete from a different thread, as a real host would.
    std::thread([&] { twk_http_respond(call.client, call.token, 200, "{}", "{\"balance\":\"7\"}"); }).join();

    auto result = fut.get();
    bool ok = args_ok && result.ok && result.status == 200 && result.body == "{\"balance\":\"7\"}";
    if (!ok) {
        printf("round_trip: args_ok=%d ok=%d status=%d body=%s\n", args_ok, result.ok, result.status,
               result.body.c_str());
    }
    delete client;
    return ok;
}

bool test_failure() {
    FakeHost host;
    g_host = &host;
    auto delegates = make_delegates();
    auto* client = new twk::Client(&delegates, nullptr);

    std::promise<twk::HttpResult> done;
    auto fut = done.get_future();
    client->startHttp("POST", "https://example.test/p", "", "{\"a\":1}",
                      [&done](twk::HttpResult r) { done.set_value(std::move(r)); });
    wait_for([&] { return host.count() == 1; });
    auto call = host.at(0);
    twk_http_failed(call.client, call.token, "connection refused");

    auto result = fut.get();
    bool ok = !result.ok && result.error == "connection refused" && call.body == "{\"a\":1}";
    if (!ok) {
        printf("failure: ok=%d error=%s\n", result.ok, result.error.c_str());
    }
    delete client;
    return ok;
}

bool test_no_delegate() {
    // No delegates at all -> immediate failure, not a hang.
    auto* client = new twk::Client(nullptr, nullptr);
    std::promise<twk::HttpResult> done;
    auto fut = done.get_future();
    int64_t token = client->startHttp("GET", "https://example.test/", "", "",
                                      [&done](twk::HttpResult r) { done.set_value(std::move(r)); });
    auto result = fut.get();
    bool ok = token == 0 && !result.ok && !result.error.empty();
    if (!ok) {
        printf("no_delegate: token=%lld ok=%d\n", static_cast<long long>(token), result.ok);
    }
    delete client;
    return ok;
}

bool test_cancel_and_late_completion() {
    FakeHost host;
    g_host = &host;
    auto delegates = make_delegates();
    auto* client = new twk::Client(&delegates, nullptr);

    std::atomic<bool> called{false};
    int64_t token = client->startHttp("GET", "https://example.test/slow", "", "",
                                      [&called](twk::HttpResult) { called.store(true); });
    wait_for([&] { return host.count() == 1; });

    client->cancelHttp(token);
    // A late completion for a cancelled token must be dropped, not crash.
    twk_http_respond(reinterpret_cast<twk_client*>(client), token, 200, "{}", "late");
    std::this_thread::sleep_for(60ms);

    bool ok = host.cancels.load() == 1 && !called.load();
    if (!ok) {
        printf("cancel: cancels=%d called=%d\n", host.cancels.load(), called.load());
    }
    delete client;
    return ok;
}

// The host's network thread cannot know the app destroyed the client, so a
// completion arriving after destroy is a real race. It must be dropped, not
// dereference freed memory (this deadlocked before the live-client registry).
bool test_completion_after_destroy() {
    FakeHost host;
    g_host = &host;
    auto delegates = make_delegates();
    auto* client = new twk::Client(&delegates, nullptr);

    client->startHttp("GET", "https://example.test/x", "", "", [](twk::HttpResult) {});
    wait_for([&] { return host.count() == 1; });
    auto call = host.at(0);

    delete client; // destroy with the request still in flight

    twk_http_respond(call.client, call.token, 200, "{}", "too late");
    twk_http_failed(call.client, call.token, "too late");
    return true;
}

} // namespace

int main() {
    struct {
        const char* name;
        bool (*fn)();
    } tests[] = {
        {"round_trip", test_round_trip},
        {"failure", test_failure},
        {"no_delegate", test_no_delegate},
        {"cancel_and_late_completion", test_cancel_and_late_completion},
        {"completion_after_destroy", test_completion_after_destroy},
    };

    for (auto& t : tests) {
        // Flush as we go: if a case hangs, the output shows which one.
        printf("run: %s\n", t.name);
        fflush(stdout);
        if (!t.fn()) {
            printf("FAIL: %s\n", t.name);
            return 1;
        }
        printf("ok: %s\n", t.name);
        fflush(stdout);
    }
    return 0;
}
