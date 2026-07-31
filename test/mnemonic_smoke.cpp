// M1 exit criterion: run createMnemonic through the real @ton/walletkit bundle
// embedded in QuickJS and assert a 24-word TON mnemonic comes back. This proves
// the canonical walletkit JS loads and runs a real crypto call in the core
// (RNG + our PBKDF2 shim included). Returns 0 on pass.
#include <cstdio>
#include <string>

#include "twk/twk.h"

int main() {
    twk_client* c = twk_client_create(nullptr, nullptr);
    twk_send(c, 1, "createMnemonic", "[]");

    unsigned long long rid = 0;
    // Generous: createMnemonic runs jssha SHA-512/HMAC in the QuickJS interpreter
    // (no JIT), so the TON mnemonic retry loop is slow (~20-40s). Native SHA/HMAC
    // shims are a tracked perf follow-up.
    const char* out = twk_receive(c, 90.0, &rid);
    if (out == nullptr) {
        printf("FAIL: timed out waiting for createMnemonic\n");
        twk_client_destroy(c);
        return 1;
    }

    std::string s(out);
    // Envelope is {"result":["w1",...,"w24"]}: 24 words -> 23 commas, no "error".
    int commas = 0;
    for (char ch : s) {
        if (ch == ',') {
            ++commas;
        }
    }
    bool ok = rid == 1 && s.rfind("{\"result\":[", 0) == 0 && s.find("\"error\"") == std::string::npos &&
              commas == 23;

    printf("createMnemonic rid=%llu commas=%d\n%.140s%s\n", rid, commas, s.c_str(), s.size() > 140 ? " ..." : "");
    twk_client_destroy(c);
    printf(ok ? "ok: 24-word mnemonic through QuickJS\n" : "FAIL: unexpected result\n");
    return ok ? 0 : 1;
}
