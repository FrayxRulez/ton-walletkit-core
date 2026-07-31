// Proves send/receive genuinely crosses native -> JS -> native: the params are
// parsed to a JS value and re-stringified, so `1e3` comes back normalized to
// `1000` (a plain C++ echo would return the literal "1e3"). Returns 0 on pass.
#include <cstdio>
#include <string>

#include "twk/twk.h"

static bool contains(const char* s, const char* needle) {
    return s && std::string(s).find(needle) != std::string::npos;
}

int main() {
    twk_client* c = twk_client_create(nullptr, nullptr);

    twk_send(c, 42, "echo", "[{\"n\":1e3,\"s\":\"hi\"}]"); // positional args: echo({n:1e3,s:'hi'})

    unsigned long long rid = 0;
    const char* out = twk_receive(c, 2.0, &rid);

    bool ok = out && rid == 42 && contains(out, "\"result\"") &&
              contains(out, "\"n\":1000") &&  // 1e3 normalized by JS JSON round-trip
              contains(out, "\"s\":\"hi\"");
    if (!ok) {
        printf("bridge_smoke: rid=%llu out=%s\n", rid, out ? out : "(null)");
    }

    twk_client_destroy(c);
    printf(ok ? "ok: bridge round-trip through JS\n" : "FAIL: bridge_smoke\n");
    return ok ? 0 : 1;
}
