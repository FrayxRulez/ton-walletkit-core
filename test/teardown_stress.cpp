// Teardown across varied in-flight states — no crashes/leaks (run under a leak
// checker in M5; here it asserts behaviour and exercises the paths). Returns 0.
//
// Note: per the ABI, receive must not race destroy; each case below stops using
// the client before destroying it.
#include <cstdio>
#include <string>

#include "twk/twk.h"

// Create and destroy immediately, no work at all.
static bool test_create_destroy() {
    for (int i = 0; i < 200; ++i) {
        twk_client* c = twk_client_create(nullptr, nullptr);
        twk_client_destroy(c);
    }
    return true;
}

// Send then destroy without ever receiving (in-flight request dropped on stop).
static bool test_send_no_receive() {
    for (int i = 0; i < 200; ++i) {
        twk_client* c = twk_client_create(nullptr, nullptr);
        twk_send(c, static_cast<unsigned long long>(i), "noop", "null");
        twk_client_destroy(c);
    }
    return true;
}

// Send several, receive only some, destroy with responses still queued.
static bool test_partial_receive() {
    for (int i = 0; i < 50; ++i) {
        twk_client* c = twk_client_create(nullptr, nullptr);
        for (int j = 0; j < 5; ++j) {
            twk_send(c, static_cast<unsigned long long>(j + 1), "echo", "null");
        }
        unsigned long long rid = 0;
        twk_receive(c, 2.0, &rid); // drain just one; leave the rest queued
        twk_client_destroy(c);
    }
    return true;
}

// Steady-state: send then receive, one at a time, correlation intact throughout.
static bool test_send_receive_loop() {
    twk_client* c = twk_client_create(nullptr, nullptr);
    bool ok = true;
    for (unsigned long long i = 1; i <= 100; ++i) {
        twk_send(c, i, "echo", "null");
        unsigned long long rid = 0;
        const char* out = twk_receive(c, 2.0, &rid);
        if (!out || rid != i) {
            printf("send_receive_loop: expected %llu, got %llu\n", i, rid);
            ok = false;
            break;
        }
    }
    twk_client_destroy(c);
    return ok;
}

int main() {
    struct {
        const char* name;
        bool (*fn)();
    } tests[] = {
        {"create_destroy", test_create_destroy},
        {"send_no_receive", test_send_no_receive},
        {"partial_receive", test_partial_receive},
        {"send_receive_loop", test_send_receive_loop},
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
