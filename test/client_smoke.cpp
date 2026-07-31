// Sanity checks for the C ABI over the native-await transport: send/receive
// correlation, the {result}/{error} envelopes, timeout, ordering, and
// create/destroy churn. Drives the stub walletKit (echo/fail). Returns 0 on pass.
#include <cstdio>
#include <string>

#include "twk/twk.h"

static bool contains(const char* s, const char* needle) {
    return s && std::string(s).find(needle) != std::string::npos;
}

static bool test_send_receive() {
    twk_client* c = twk_client_create(nullptr, nullptr);
    twk_send(c, 7, "echo", "[{\"x\":1}]"); // positional args: echo({x:1})

    unsigned long long rid = 0;
    const char* out = twk_receive(c, 2.0, &rid);

    bool ok = out && rid == 7 && contains(out, "\"result\"") && contains(out, "\"x\":1");
    if (!ok) {
        printf("send_receive: rid=%llu out=%s\n", rid, out ? out : "(null)");
    }
    twk_client_destroy(c);
    return ok;
}

static bool test_error() {
    twk_client* c = twk_client_create(nullptr, nullptr);

    twk_send(c, 1, "fail", "[]"); // stub throws -> {error}
    unsigned long long rid = 0;
    const char* out = twk_receive(c, 2.0, &rid);
    bool ok = out && rid == 1 && contains(out, "\"error\"");

    twk_send(c, 2, "doesNotExist", "[]"); // unknown method -> {error}
    const char* out2 = twk_receive(c, 2.0, &rid);
    ok = ok && out2 && rid == 2 && contains(out2, "unknown method");

    if (!ok) {
        printf("error: out=%s\n", out ? out : "(null)");
    }
    twk_client_destroy(c);
    return ok;
}

static bool test_timeout() {
    twk_client* c = twk_client_create(nullptr, nullptr);

    unsigned long long rid = 999;
    const char* out = twk_receive(c, 0.05, &rid);

    bool ok = out == nullptr;
    if (!ok) {
        printf("timeout: expected null, got %s\n", out);
    }
    twk_client_destroy(c);
    return ok;
}

static bool test_ordering() {
    twk_client* c = twk_client_create(nullptr, nullptr);
    twk_send(c, 1, "echo", "[1]");
    twk_send(c, 2, "echo", "[2]");
    twk_send(c, 3, "echo", "[3]");

    bool ok = true;
    for (unsigned long long expect = 1; expect <= 3; ++expect) {
        unsigned long long rid = 0;
        const char* out = twk_receive(c, 2.0, &rid);
        if (!out || rid != expect) {
            printf("ordering: expected rid %llu, got %llu\n", expect, rid);
            ok = false;
            break;
        }
    }
    twk_client_destroy(c);
    return ok;
}

static bool test_churn() {
    for (int i = 0; i < 5; ++i) {
        twk_client* c = twk_client_create(nullptr, nullptr);
        twk_send(c, static_cast<unsigned long long>(i), "echo", "null");
        twk_client_destroy(c);
    }
    return true;
}

int main() {
    struct {
        const char* name;
        bool (*fn)();
    } tests[] = {
        {"send_receive", test_send_receive}, {"error", test_error},   {"timeout", test_timeout},
        {"ordering", test_ordering},         {"churn", test_churn},
    };

    for (auto& t : tests) {
        if (!t.fn()) {
            printf("FAIL: %s\n", t.name);
            return 1;
        }
        printf("ok: %s\n", t.name);
    }
    return 0;
}
