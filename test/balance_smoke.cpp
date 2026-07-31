// M2 exit criterion, deterministic variant: init the kit and read a balance
// through walletkit's own ApiClientToncenter, with the host delegate replaying a
// recorded /api/v3/addressInformation response instead of touching the network.
// Also covers the HTTP-error and malformed-body paths. Returns 0 on pass.
//
// (twk_refhost_smoke does the same against the live endpoint, opt-in.)
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "twk/twk.h"
#include "twk/twk_delegates.h"

namespace {

// Recorded from testnet.toncenter.com for the elector (-1:3333…), trimmed to the
// fields getAccountState reads.
const char* kAddressInformation =
    "{\"balance\":\"110576459116021734\","
    "\"code\":\"te6ccgECZAEADyIAART/APSkE/S88sgLAQIBIAIDAgFIBAU=\","
    "\"data\":\"te6ccgEBAQEAAgAAAA==\","
    "\"last_transaction_lt\":\"36612000000003\","
    "\"last_transaction_hash\":\"YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXoxMjM0NTY3OA==\","
    "\"status\":\"active\"}";

enum class Mode { Ok, ServerError, Malformed };

std::atomic<Mode> g_mode{Mode::Ok};
std::mutex g_mutex;
std::vector<std::string> g_urls;

void on_http(void* /*user*/, twk_client* client, twk_token token, const char* /*method*/, const char* url,
             const char* /*headers_json*/, const char* /*body*/) {
    {
        std::lock_guard<std::mutex> guard(g_mutex);
        g_urls.push_back(url ? url : "");
    }

    const char* json_headers = "{\"content-type\":\"application/json\"}";
    switch (g_mode.load()) {
        case Mode::Ok:
            twk_http_respond(client, token, 200, json_headers, kAddressInformation);
            break;
        case Mode::ServerError:
            twk_http_respond(client, token, 500, json_headers, "{\"error\":\"internal\"}");
            break;
        case Mode::Malformed:
            twk_http_respond(client, token, 200, json_headers, "not json at all");
            break;
    }
}

bool contains(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

std::string call(twk_client* c, unsigned long long id, const char* method, const char* params) {
    twk_send(c, id, method, params);
    unsigned long long rid = 0;
    const char* out = twk_receive(c, 60.0, &rid);
    return out ? std::string(out) : std::string();
}

const char* kElector = "-1:3333333333333333333333333333333333333333333333333333333333333333";

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);

    twk_delegates delegates{};
    delegates.http_request = on_http;
    twk_client* c = twk_client_create(&delegates, nullptr);

    bool ok = true;

    // init: build the kit with a testnet ApiClientToncenter.
    std::string result =
        call(c, 1, "initWalletKit", "[{\"networks\":[{\"chainId\":\"-3\",\"endpoint\":\"https://testnet.toncenter.com\"}]}]");
    bool got = contains(result, "\"networks\"") && contains(result, "-3");
    printf("%s: init -> %.90s\n", got ? "ok" : "FAIL", result.c_str());
    ok = ok && got;

    // getBalance through walletkit's own client (not a diagnostic shortcut).
    std::string params = std::string("[\"") + kElector + "\",\"-3\"]";
    result = call(c, 2, "getAddressBalance", params.c_str());
    got = contains(result, "\"balance\":\"110576459116021734\"");
    printf("%s: getBalance -> %.140s\n", got ? "ok" : "FAIL", result.c_str());
    ok = ok && got;

    // The request walletkit actually built must hit the right endpoint.
    {
        std::lock_guard<std::mutex> guard(g_mutex);
        bool routed = false;
        for (const auto& url : g_urls) {
            routed = routed || (contains(url, "/api/v3/addressInformation") && contains(url, "3333"));
        }
        printf("%s: request routed to addressInformation (%zu request(s))\n", routed ? "ok" : "FAIL", g_urls.size());
        ok = ok && routed;
    }

    // HTTP 500 -> the call rejects rather than yielding a bogus balance.
    g_mode.store(Mode::ServerError);
    result = call(c, 3, "getAddressBalance", params.c_str());
    got = contains(result, "\"error\"") && !contains(result, "\"balance\"");
    printf("%s: HTTP 500 -> %.110s\n", got ? "ok" : "FAIL", result.c_str());
    ok = ok && got;

    // Malformed body -> rejects too.
    g_mode.store(Mode::Malformed);
    result = call(c, 4, "getAddressBalance", params.c_str());
    got = contains(result, "\"error\"") && !contains(result, "\"balance\"");
    printf("%s: malformed body -> %.110s\n", got ? "ok" : "FAIL", result.c_str());
    ok = ok && got;

    twk_client_destroy(c);
    printf(ok ? "PASS\n" : "FAILED\n");
    return ok ? 0 : 1;
}
