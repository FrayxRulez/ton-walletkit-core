//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// Robustness: bad input and misbehaving hosts must produce errors, not crashes,
// hangs, or corrupted correlation. Everything here is something a buggy binding
// or a hostile dapp could actually do. Returns 0 on pass.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "twk/twk.h"
#include "twk/twk_delegates.h"

namespace {

// A deliberately misbehaving host: it answers, then answers again, and also
// completes tokens it was never given.
void on_http(void* /*user*/, twk_client* client, twk_token token, const char* /*method*/, const char* /*url*/,
             const char* /*headers*/, const char* /*body*/) {
    const char* headers = "{\"content-type\":\"application/json\"}";
    twk_http_respond(client, token, 200, headers, "{}");
    twk_http_respond(client, token, 200, headers, "{}");        // duplicate completion
    twk_http_failed(client, token, "late failure");             // after already completing
    twk_http_respond(client, 999999, 200, headers, "{}");       // token never issued
    twk_storage_respond(client, 888888, "ghost");               // wrong family entirely
    twk_sse_event(client, 777777, "{\"data\":\"ghost\"}");      // ditto
}

bool contains(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

struct Message {
    unsigned long long request_id;
    std::string json;
};

Message receive(twk_client* c, double timeout = 60.0) {
    unsigned long long rid = 0;
    const char* out = twk_receive(c, timeout, &rid);
    return {rid, out ? std::string(out) : std::string()};
}

// Sends and waits for this id's response (skipping any events).
std::string call(twk_client* c, unsigned long long id, const char* method, const char* params) {
    twk_send(c, id, method, params);
    for (int i = 0; i < 10; ++i) {
        Message m = receive(c);
        if (m.json.empty()) {
            return "<timeout>";
        }
        if (m.request_id == id) {
            return m.json;
        }
    }
    return "<no response>";
}

// Every input here must yield an error (or a benign result) — never a hang.
struct Case {
    const char* name;
    const char* method;
    const char* params;
};

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);

    twk_delegates delegates{};
    delegates.http_request = on_http;
    twk_client* c = twk_client_create(&delegates, nullptr);

    bool ok = true;

    std::string result = call(c, 1, "initWalletKit", "[{\"networks\":[{\"chainId\":\"-3\"}]}]");
    bool got = contains(result, "\"result\"");
    printf("%s: initWalletKit (host completes twice + unknown tokens)\n", got ? "ok" : "FAIL");
    ok = ok && got;

    const Case cases[] = {
        {"unknown method", "noSuchMethod", "[]"},
        {"malformed JSON params", "echo", "{not json at all"},
        {"params not an array", "echo", "\"bare string\""},
        {"null params", "echo", "null"},
        {"empty method name", "", "[]"},
        {"wrong arg type", "getBalance", "[12345]"},
        {"missing required args", "createSignerFromMnemonic", "[]"},
        {"deeply nested payload", "echo", "[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]"},
        {"unicode + escapes", "echo", "[\"\\u0000\\ud83d\\ude00 \\\" \\\\ \"]"},
        {"prototype-pollution attempt", "echo", "[{\"__proto__\":{\"polluted\":true}}]"},
    };

    unsigned long long id = 10;
    for (const Case& test : cases) {
        result = call(c, id++, test.method, test.params);
        // The contract: a response comes back, correlated, and the process lives.
        bool survived = result != "<timeout>" && result != "<no response>" && !result.empty();
        printf("%s: %-28s -> %.70s\n", survived ? "ok" : "FAIL", test.name, result.c_str());
        ok = ok && survived;
    }

    // A very large payload must not wedge anything.
    {
        std::string big = "[\"" + std::string(2 * 1024 * 1024, 'x') + "\"]"; // 2 MB string
        result = call(c, 100, "echo", big.c_str());
        got = result != "<timeout>" && !result.empty();
        printf("%s: 2MB payload (%zu bytes back)\n", got ? "ok" : "FAIL", result.size());
        ok = ok && got;
    }

    // Prototype pollution must not have taken hold.
    result = call(c, 200, "echo", "[{}]");
    got = !contains(result, "polluted");
    printf("%s: prototype not polluted\n", got ? "ok" : "FAIL");
    ok = ok && got;

    // A runaway script must be interrupted, not wedge the worker thread forever.
    // TWK_JS_BUDGET_MS (set by the test runner) keeps the budget short here.
    if (std::getenv("TWK_JS_BUDGET_MS") != nullptr) {
        auto started = std::chrono::steady_clock::now();
        result = call(c, 250, "spinForever", "[]");
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started);
        got = result != "<timeout>" && contains(result, "\"error\"") && elapsed < std::chrono::seconds(30);
        printf("%s: runaway script interrupted after %llds -> %.70s\n", got ? "ok" : "FAIL",
               static_cast<long long>(elapsed.count()), result.c_str());
        ok = ok && got;
    }

    // After all that abuse, ordinary calls still work and still correlate.
    result = call(c, 300, "echo", "[\"still alive\"]");
    got = contains(result, "still alive");
    printf("%s: client still healthy after abuse\n", got ? "ok" : "FAIL");
    ok = ok && got;

    twk_client_destroy(c);
    printf(ok ? "PASS\n" : "FAILED\n");
    return ok ? 0 : 1;
}
