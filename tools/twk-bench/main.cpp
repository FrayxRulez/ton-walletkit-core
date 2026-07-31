//
// twk-bench — where does the time go? Splits client startup (bundle load) from
// per-call cost, so perf work is measured rather than guessed.
//
// Usage: twk-bench [iterations]   (default 3)
//
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "twk/twk.h"

using clock_type = std::chrono::steady_clock;

static double ms_since(clock_type::time_point start) {
    return std::chrono::duration<double, std::milli>(clock_type::now() - start).count();
}

// Send one request and wait for its response; returns elapsed ms (-1 on timeout).
static double timed_call(twk_client* c, unsigned long long id, const char* method, const char* params,
                         std::string* out_json = nullptr) {
    auto start = clock_type::now();
    twk_send(c, id, method, params);
    unsigned long long rid = 0;
    const char* out = twk_receive(c, 120.0, &rid);
    double elapsed = ms_since(start);
    if (out == nullptr) {
        return -1;
    }
    if (out_json != nullptr) {
        *out_json = out;
    }
    return elapsed;
}

struct Stats {
    double min = 0, max = 0, mean = 0;
};

static Stats summarize(const std::vector<double>& xs) {
    Stats s;
    if (xs.empty()) {
        return s;
    }
    s.min = s.max = xs[0];
    double total = 0;
    for (double x : xs) {
        s.min = x < s.min ? x : s.min;
        s.max = x > s.max ? x : s.max;
        total += x;
    }
    s.mean = total / static_cast<double>(xs.size());
    return s;
}

static void report(const char* label, const std::vector<double>& xs) {
    Stats s = summarize(xs);
    std::printf("%-28s mean %8.1f ms   min %8.1f   max %8.1f   (n=%zu)\n", label, s.mean, s.min, s.max, xs.size());
}

int main(int argc, char** argv) {
    int iterations = argc > 1 ? std::atoi(argv[1]) : 3;
    if (iterations < 1) {
        iterations = 1;
    }

    std::vector<double> create_ms, first_call_ms, echo_ms, mnemonic_ms, destroy_ms;

    for (int i = 0; i < iterations; ++i) {
        // Startup: create (spawns the worker thread) + the first call, which can only
        // complete once the bundle has been evaluated and __twk_ready has fired.
        auto t0 = clock_type::now();
        twk_client* c = twk_client_create(nullptr, nullptr);
        create_ms.push_back(ms_since(t0));

        first_call_ms.push_back(timed_call(c, 1, "echo", "[1]"));

        // Warm per-call baseline (transport + JSON only).
        double warm = 0;
        const int kEcho = 20;
        for (int j = 0; j < kEcho; ++j) {
            warm += timed_call(c, static_cast<unsigned long long>(100 + j), "echo", "[1]");
        }
        echo_ms.push_back(warm / kEcho);

        // The real crypto workload.
        std::string result;
        mnemonic_ms.push_back(timed_call(c, 2, "createMnemonic", "[]", &result));

        auto t1 = clock_type::now();
        twk_client_destroy(c);
        destroy_ms.push_back(ms_since(t1));

        std::printf("iter %d: startup %.0f ms (create %.0f + first call %.0f), mnemonic %.0f ms\n", i + 1,
                    create_ms.back() + first_call_ms.back(), create_ms.back(), first_call_ms.back(),
                    mnemonic_ms.back());
    }

    std::printf("\n--- twk-bench (%d iterations) ---\n", iterations);
    report("client_create", create_ms);
    report("first call (bundle load)", first_call_ms);
    report("echo (warm round trip)", echo_ms);
    report("createMnemonic", mnemonic_ms);
    report("client_destroy", destroy_ms);
    return 0;
}
