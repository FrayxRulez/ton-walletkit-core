// Drives the core against a real endpoint through the reference host, proving the
// whole chain works with no platform integration:
//   JS fetch -> __twk_http -> http_request delegate -> WinHTTP -> twk_http_respond.
//
// This test touches the network. It is opt-in via TWK_LIVE_NETWORK_TESTS=1 so CI
// stays deterministic (M2 also adds recorded fixtures); without it the test skips
// and passes.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "reference_host.h"
#include "twk/twk.h"

static bool contains(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);

    const char* live = std::getenv("TWK_LIVE_NETWORK_TESTS");
    if (live == nullptr || std::string(live) != "1") {
        printf("SKIP: set TWK_LIVE_NETWORK_TESTS=1 to run live-network tests\n");
        return 0;
    }
    if (!twk::refhost::ReferenceHost::httpAvailable()) {
        printf("SKIP: reference host has no HTTP backend on this platform\n");
        return 0;
    }

    twk::refhost::ReferenceHost host;
    twk_client* c = twk_client_create(host.delegates(), host.userData());

    // The TON testnet elector: a system contract, so it always exists and holds a
    // large balance — a stable target that needs no funding.
    const char* url =
        "\"https://testnet.toncenter.com/api/v3/addressInformation"
        "?address=-1%3A3333333333333333333333333333333333333333333333333333333333333333\"";
    std::string params = std::string("[") + url + "]";

    twk_send(c, 1, "httpProbe", params.c_str());
    unsigned long long rid = 0;
    const char* out = twk_receive(c, 60.0, &rid);
    std::string result = out ? out : "";

    bool ok = !result.empty() && contains(result, "\"ok\":true") && contains(result, "\"status\":200") &&
              contains(result, "balance");
    printf("%s: live toncenter fetch -> %.200s\n", ok ? "ok" : "FAIL", result.c_str());

    twk_client_destroy(c);
    return ok ? 0 : 1;
}
