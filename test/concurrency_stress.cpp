//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// Concurrency stress. MSVC ships no ThreadSanitizer, so this cannot *prove* the
// absence of data races; its job is to make races likely enough to surface, and
// to be run under AddressSanitizer (scripts\win-asan.bat) where a torn read of a
// freed object usually does show up.
//
// Invariants under concurrent load:
//   1. every request gets exactly one response — none lost, none duplicated;
//   2. no response carries an id that was never sent;
//   3. destroy racing in-flight host completions neither crashes nor hangs.
//
// The ABI says receive must not be called from two threads for one client, so the
// shape is many senders + one receiver — the documented usage.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "twk/twk.h"
#include "twk/twk_delegates.h"

using namespace std::chrono_literals;

namespace {

// Completes on a detached thread, so completions land on threads the core does
// not own — the arrangement most likely to expose a lifetime bug.
struct AsyncHost {
    std::atomic<int> completed{0};
    std::mutex mutex;
    std::vector<std::thread> threads;

    void join() {
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
};

AsyncHost* g_host = nullptr;

void on_http(void* /*user*/, twk_client* client, twk_token token, const char* /*method*/, const char* /*url*/,
             const char* /*headers*/, const char* /*body*/) {
    std::lock_guard<std::mutex> guard(g_host->mutex);
    g_host->threads.emplace_back([client, token] {
        std::this_thread::sleep_for(std::chrono::milliseconds(token % 5));
        twk_http_respond(client, token, 200, "{\"content-type\":\"application/json\"}", "{}");
        g_host->completed.fetch_add(1);
    });
}

bool contains(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

// Many senders, one receiver: every response must come back exactly once.
bool test_concurrent_senders() {
    twk_delegates delegates{};
    delegates.http_request = on_http;
    AsyncHost host;
    g_host = &host;

    twk_client* c = twk_client_create(&delegates, nullptr);

    // Wait for the bundle so the run measures contention, not startup.
    twk_send(c, 1, "echo", "[1]");
    unsigned long long rid = 0;
    twk_receive(c, 60.0, &rid);

    const int kThreads = 8;
    const int kPerThread = 40;
    const int kTotal = kThreads * kPerThread;

    std::atomic<bool> go{false};
    std::vector<std::thread> senders;
    for (int t = 0; t < kThreads; ++t) {
        senders.emplace_back([c, t, kPerThread, &go] {
            while (!go.load()) {
                std::this_thread::yield(); // release them together
            }
            for (int i = 0; i < kPerThread; ++i) {
                unsigned long long id = 1000ull + static_cast<unsigned long long>(t) * 1000 + i;
                twk_send(c, id, "echo", "[\"x\"]");
            }
        });
    }
    go.store(true);

    std::unordered_map<unsigned long long, int> seen;
    int received = 0;
    auto deadline = std::chrono::steady_clock::now() + 120s;
    while (received < kTotal && std::chrono::steady_clock::now() < deadline) {
        unsigned long long id = 0;
        const char* out = twk_receive(c, 5.0, &id);
        if (out == nullptr) {
            continue;
        }
        if (id == 0) {
            continue; // an event, not a response
        }
        ++seen[id];
        ++received;
    }

    for (auto& t : senders) {
        t.join();
    }

    bool ok = received == kTotal && static_cast<int>(seen.size()) == kTotal;
    for (const auto& [id, count] : seen) {
        bool in_range = id >= 1000 && id < 1000 + kThreads * 1000ull + kPerThread;
        ok = ok && count == 1 && in_range; // exactly once, and an id we sent
    }
    printf("%s: %d/%d responses, %zu unique ids, none duplicated\n", ok ? "ok" : "FAIL", received, kTotal,
           seen.size());

    twk_client_destroy(c);
    host.join();
    return ok;
}

// Destroy while host completions are in flight on other threads.
bool test_destroy_racing_completions() {
    twk_delegates delegates{};
    delegates.http_request = on_http;

    for (int round = 0; round < 8; ++round) {
        AsyncHost host;
        g_host = &host;

        twk_client* c = twk_client_create(&delegates, nullptr);
        twk_send(c, 1, "initWalletKit", "[{\"networks\":[{\"chainId\":\"-3\"}]}]");

        // Tear down at a varying point so destroy lands in different phases —
        // sometimes mid-bundle-load, sometimes with HTTP completions pending.
        std::this_thread::sleep_for(std::chrono::milliseconds(round * 120));
        twk_client_destroy(c);

        // Completions now fire against a destroyed client; the live-client
        // registry must make that a no-op rather than a use-after-free.
        host.join();
    }
    printf("ok: destroy racing in-flight completions (8 rounds)\n");
    return true;
}

// Senders continuing while the client is destroyed underneath is NOT legal per
// the ABI, so it is deliberately not tested: send/receive after destroy is
// caller error. What is tested is the host-completion race, which is unavoidable.

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);

    bool ok = true;
    ok &= test_concurrent_senders();
    ok &= test_destroy_racing_completions();

    printf(ok ? "PASS\n" : "FAILED\n");
    return ok ? 0 : 1;
}
