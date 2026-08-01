//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// M4 exit criterion: a TON Connect connect request arrives as an unsolicited
// update and can be approved.
//
//   handleTonConnectUrl(tc://…) -> {"event":{"type":"connectRequest",…}} at
//   request_id 0 -> approveConnectRequest(event, …) round-trips.
//
// A real handshake needs a dapp on the other end, so the dapp side is fixtures:
// the connect request is carried in the URL (walletkit parses it there), the
// manifest fetch and the bridge POST are served by the http delegate, and the
// relay is an SSE stream we control. Returns 0 on pass.
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "twk/twk.h"
#include "twk/twk_delegates.h"

namespace {

std::mutex g_mutex;
std::vector<std::string> g_urls;
std::atomic<bool> g_sse_open{false};

const char* kManifest =
    "{\"url\":\"https://dapp.test\",\"name\":\"Fixture Dapp\","
    "\"iconUrl\":\"https://dapp.test/icon.png\"}";

void on_http(void* /*user*/, twk_client* client, twk_token token, const char* /*method*/, const char* url,
             const char* /*headers*/, const char* /*body*/) {
    std::string u = url ? url : "";
    {
        std::lock_guard<std::mutex> guard(g_mutex);
        g_urls.push_back(u);
    }
    const char* headers = "{\"content-type\":\"application/json\"}";

    if (u.find("manifest") != std::string::npos) {
        twk_http_respond(client, token, 200, headers, kManifest);
    } else if (u.find("/message") != std::string::npos || u.find("bridge") != std::string::npos) {
        twk_http_respond(client, token, 200, headers, "{\"statusCode\":200,\"message\":\"OK\"}");
    } else {
        twk_http_respond(client, token, 200, headers, "{}");
    }
}

void on_sse_open(void* /*user*/, twk_client* /*client*/, twk_token /*token*/, const char* /*url*/,
                 const char* /*headers*/) {
    g_sse_open.store(true); // the relay stream; no frames needed for this flow
}
void on_sse_close(void* /*user*/, twk_client* /*client*/, twk_token /*token*/) {}

void on_storage_get(void* /*user*/, twk_client* client, twk_token token, const char* /*key*/) {
    twk_storage_respond(client, token, nullptr);
}
void on_storage_write(void* /*user*/, twk_client* client, twk_token token, const char* /*key*/,
                      const char* /*value*/) {
    twk_storage_respond(client, token, "");
}
void on_storage_remove(void* /*user*/, twk_client* client, twk_token token, const char* /*key*/) {
    twk_storage_respond(client, token, "");
}
void on_storage_clear(void* /*user*/, twk_client* client, twk_token token) {
    twk_storage_respond(client, token, "");
}

bool contains(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

struct Message {
    unsigned long long request_id;
    std::string json;
};

Message receive(twk_client* c, double timeout = 30.0) {
    unsigned long long rid = 0;
    const char* out = twk_receive(c, timeout, &rid);
    return {rid, out ? std::string(out) : std::string()};
}

std::string call(twk_client* c, unsigned long long id, const char* method, const std::string& params) {
    twk_send(c, id, method, params.c_str());
    for (int i = 0; i < 12; ++i) {
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

// A TON Connect v2 universal link. `id` is the dapp's session public key (32
// bytes hex); `r` is the connect request the wallet must show the user.
std::string connectUrl() {
    std::string client_id(64, 'a');
    // r={"manifestUrl":"https://dapp.test/manifest.json","items":[{"name":"ton_addr"}]}
    std::string request =
        "%7B%22manifestUrl%22%3A%22https%3A%2F%2Fdapp.test%2Fmanifest.json%22%2C%22items%22%3A%5B%7B%22name"
        "%22%3A%22ton_addr%22%7D%5D%7D";
    return "tc://?v=2&id=" + client_id + "&r=" + request + "&ret=back";
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);

    twk_delegates delegates{};
    delegates.http_request = on_http;
    delegates.sse_open = on_sse_open;
    delegates.sse_close = on_sse_close;
    delegates.storage_get = on_storage_get;
    delegates.storage_set = on_storage_write;
    delegates.storage_remove = on_storage_remove;
    delegates.storage_clear = on_storage_clear;

    twk_client* c = twk_client_create(&delegates, nullptr);
    bool ok = true;

    std::string result = call(c, 1, "initWalletKit",
                              "[{\"networks\":[{\"chainId\":\"-3\",\"endpoint\":\"https://testnet.toncenter.com\"}],"
                              "\"bridge\":{\"bridgeUrl\":\"https://bridge.test/bridge\","
                              "\"jsBridgeKey\":\"twk\",\"deviceInfo\":{\"platform\":\"windows\","
                              "\"appName\":\"twk-test\",\"appVersion\":\"0.1\",\"maxProtocolVersion\":2,"
                              "\"features\":[]},\"walletInfo\":{\"name\":\"twk\",\"image\":\"\","
                              "\"about_url\":\"https://example.test\"}}}]");
    bool got = contains(result, "\"networks\"");
    printf("%s: initWalletKit\n", got ? "ok" : "FAIL");
    ok = ok && got;

    // A wallet must exist for a connect request to be approvable.
    result = call(c, 2, "createSignerFromMnemonic",
                  "[[\"adult\",\"maid\",\"prison\",\"crash\",\"media\",\"weather\",\"paper\",\"virus\",\"wheat\","
                  "\"rude\",\"mesh\",\"fit\",\"boost\",\"sphere\",\"imitate\",\"capable\",\"path\",\"invest\","
                  "\"spider\",\"episode\",\"magnet\",\"tongue\",\"address\",\"climb\"],\"ton\"]");
    std::string signer = contains(result, "signerId") ? "signer:1" : "";
    result = call(c, 3, "createV5R1WalletAdapter", "[\"" + signer + "\",{\"chainId\":\"-3\"}]");
    std::string adapter = contains(result, "adapterId") ? "adapter:2" : "";
    result = call(c, 4, "addWallet", "[\"" + adapter + "\"]");
    got = contains(result, "walletId");
    std::string wallet_id;
    if (got) {
        const char* key = "\"walletId\":\"";
        size_t start = result.find(key) + strlen(key);
        wallet_id = result.substr(start, result.find('"', start) - start);
    }
    printf("%s: wallet ready for approval\n", got ? "ok" : "FAIL");
    ok = ok && got;

    // The flow under test.
    std::string connect_params = "[\"" + connectUrl() + "\"]";
    twk_send(c, 5, "handleTonConnectUrl", connect_params.c_str());

    // Collect messages until the connect-request event shows up (its own response
    // may arrive first — order is not guaranteed).
    std::string event_json;
    std::string handle_response;
    for (int i = 0; i < 12 && event_json.empty(); ++i) {
        Message m = receive(c);
        if (m.json.empty()) {
            break;
        }
        if (m.request_id == 0 && contains(m.json, "connectRequest")) {
            event_json = m.json;
        } else if (m.request_id == 5) {
            handle_response = m.json;
        }
    }

    got = !event_json.empty();
    printf("%s: connect request delivered as an update (request_id=0)\n   %.200s\n", got ? "ok" : "FAIL",
           event_json.c_str());
    ok = ok && got;

    if (!handle_response.empty()) {
        printf("   handleTonConnectUrl -> %.140s\n", handle_response.c_str());
    }

    // Approve it, passing the event payload straight back.
    if (got) {
        const char* key = "\"payload\":";
        size_t start = event_json.find(key);
        std::string payload = event_json.substr(start + strlen(key));
        size_t tail = payload.rfind("}}");
        if (tail != std::string::npos) {
            payload = payload.substr(0, tail); // strip the event envelope
        }
        // The host must choose which wallet to connect with by setting walletId on
        // the event — that is the user picking an account. Without it walletkit
        // rejects the approval with WALLET_REQUIRED.
        if (!wallet_id.empty() && payload.size() > 1 && payload.front() == '{') {
            payload.insert(1, "\"walletId\":\"" + wallet_id + "\",");
        }

        result = call(c, 6, "approveConnectRequest", "[" + payload + "]");
        bool approved = !contains(result, "\"error\"") && result != "<timeout>";
        printf("%s: approveConnectRequest succeeded\n   %.190s\n", approved ? "ok" : "FAIL", result.c_str());
        ok = ok && approved;
    }

    {
        std::lock_guard<std::mutex> guard(g_mutex);
        printf("note: %zu HTTP request(s), sse_open=%d\n", g_urls.size(), g_sse_open.load() ? 1 : 0);
        for (size_t i = 0; i < g_urls.size() && i < 4; ++i) {
            printf("      %.110s\n", g_urls[i].c_str());
        }
    }

    twk_client_destroy(c);
    printf(ok ? "PASS\n" : "FAILED\n");
    return ok ? 0 : 1;
}
