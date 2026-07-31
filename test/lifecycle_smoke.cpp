// M3 exit criterion: the full wallet lifecycle, deterministic (recorded toncenter
// responses, no network):
//   mnemonic -> signer -> V5R1 adapter -> addWallet -> getBalance
//            -> createTransferTonTransaction -> getSignedSendTransaction (fake sig)
//
// Then the persistence model that actually applies: walletkit persists neither
// wallets nor signers, so the host stores the mnemonic through the storage
// delegate and rebuilds signer -> adapter -> wallet on a fresh client. The test
// asserts the rebuilt wallet has the same address — which is what makes a restored
// wallet the *same* wallet.
//
// Signing always uses fakeSignature, so nothing here can produce a broadcastable
// transaction. Returns 0 on pass.
#include <algorithm>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "twk/twk.h"
#include "twk/twk_delegates.h"

namespace {

// --- recorded toncenter responses -----------------------------------------

const char* kAddressInformation =
    "{\"balance\":\"250000000\",\"status\":\"active\","
    "\"code\":\"te6ccgEBAQEAAgAAAA==\",\"data\":\"te6ccgEBAQEAAgAAAA==\","
    "\"last_transaction_lt\":\"36612000000003\","
    "\"last_transaction_hash\":\"YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXoxMjM0NTY3OA==\"}";

// seqno for a fresh, not-yet-deployed wallet.
const char* kRunGetMethod = "{\"exit_code\":0,\"gas_used\":0,\"stack\":[{\"type\":\"num\",\"value\":\"0x0\"}]}";

const char* kWalletInformation =
    "{\"balance\":\"250000000\",\"status\":\"active\",\"seqno\":0,\"wallet_type\":\"wallet v5r1\","
    "\"wallet_id\":2147483409,\"last_transaction_lt\":\"36612000000003\","
    "\"last_transaction_hash\":\"YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXoxMjM0NTY3OA==\"}";

const char* kMasterchainInfo =
    "{\"last\":{\"workchain\":-1,\"shard\":\"8000000000000000\",\"seqno\":75086804,"
    "\"root_hash\":\"YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXoxMjM0NTY3OA==\","
    "\"file_hash\":\"YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXoxMjM0NTY3OA==\"}}";

std::mutex g_mutex;
std::vector<std::string> g_unmatched;

// Storage lives outside the host struct so it survives a client, the way a real
// secure store does.
std::map<std::string, std::string> g_store;

void on_http(void* /*user*/, twk_client* client, twk_token token, const char* /*method*/, const char* url,
             const char* /*headers_json*/, const char* /*body*/) {
    std::string u = url ? url : "";
    const char* headers = "{\"content-type\":\"application/json\"}";
    auto has = [&](const char* needle) { return u.find(needle) != std::string::npos; };

    if (has("/addressInformation") || has("/accountStates")) {
        twk_http_respond(client, token, 200, headers, kAddressInformation);
    } else if (has("/runGetMethod")) {
        twk_http_respond(client, token, 200, headers, kRunGetMethod);
    } else if (has("/walletInformation")) {
        twk_http_respond(client, token, 200, headers, kWalletInformation);
    } else if (has("/masterchainInfo")) {
        twk_http_respond(client, token, 200, headers, kMasterchainInfo);
    } else {
        {
            std::lock_guard<std::mutex> guard(g_mutex);
            g_unmatched.push_back(u);
        }
        twk_http_respond(client, token, 200, headers, "{}");
    }
}

void on_storage_get(void* /*user*/, twk_client* client, twk_token token, const char* key) {
    auto it = g_store.find(key ? key : "");
    twk_storage_respond(client, token, it == g_store.end() ? nullptr : it->second.c_str());
}
void on_storage_set(void* /*user*/, twk_client* client, twk_token token, const char* key, const char* value) {
    g_store[key ? key : ""] = value ? value : "";
    twk_storage_respond(client, token, "");
}
void on_storage_remove(void* /*user*/, twk_client* client, twk_token token, const char* key) {
    g_store.erase(key ? key : "");
    twk_storage_respond(client, token, "");
}
void on_storage_clear(void* /*user*/, twk_client* client, twk_token token) {
    g_store.clear();
    twk_storage_respond(client, token, "");
}

twk_delegates make_delegates() {
    twk_delegates d{};
    d.http_request = on_http;
    d.storage_get = on_storage_get;
    d.storage_set = on_storage_set;
    d.storage_remove = on_storage_remove;
    d.storage_clear = on_storage_clear;
    return d;
}

bool contains(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

std::string call(twk_client* c, unsigned long long id, const char* method, const std::string& params) {
    twk_send(c, id, method, params.c_str());
    unsigned long long rid = 0;
    const char* out = twk_receive(c, 90.0, &rid);
    return out ? std::string(out) : std::string("<timeout>");
}

// Extract a "key":"value" string field from a JSON response.
std::string field(const std::string& json, const char* key) {
    std::string needle = std::string("\"") + key + "\":\"";
    size_t start = json.find(needle);
    if (start == std::string::npos) {
        return {};
    }
    start += needle.size();
    size_t end = json.find('"', start);
    return end == std::string::npos ? std::string() : json.substr(start, end - start);
}

const char* kInitConfig = "[{\"networks\":[{\"chainId\":\"-3\",\"endpoint\":\"https://testnet.toncenter.com\"}]}]";

// The mnemonic the host persists and restores from.
const char* kMnemonicJson =
    "[[\"adult\",\"maid\",\"prison\",\"crash\",\"media\",\"weather\",\"paper\",\"virus\",\"wheat\",\"rude\","
    "\"mesh\",\"fit\",\"boost\",\"sphere\",\"imitate\",\"capable\",\"path\",\"invest\",\"spider\",\"episode\","
    "\"magnet\",\"tongue\",\"address\",\"climb\"],\"ton\"]";

// Runs signer -> adapter -> addWallet and returns the wallet's address.
std::string buildWallet(twk_client* c, bool& ok, const char* label) {
    std::string result = call(c, 10, "createSignerFromMnemonic", kMnemonicJson);
    std::string signerId = field(result, "signerId");
    if (signerId.empty()) {
        printf("FAIL [%s] createSignerFromMnemonic -> %.120s\n", label, result.c_str());
        ok = false;
        return {};
    }

    result = call(c, 11, "createV5R1WalletAdapter", "[\"" + signerId + "\",{\"chainId\":\"-3\"}]");
    std::string adapterId = field(result, "adapterId");
    if (adapterId.empty()) {
        printf("FAIL [%s] createV5R1WalletAdapter -> %.160s\n", label, result.c_str());
        ok = false;
        return {};
    }

    result = call(c, 12, "addWallet", "[\"" + adapterId + "\"]");
    std::string address = field(result, "address");
    if (address.empty()) {
        printf("FAIL [%s] addWallet -> %.160s\n", label, result.c_str());
        ok = false;
        return {};
    }
    printf("ok: [%s] signer -> adapter -> wallet (%s)\n", label, address.c_str());
    return address;
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    bool ok = true;
    twk_delegates delegates = make_delegates();

    std::string first_address;
    std::string wallet_id;

    // ---- pass 1: build the wallet and exercise it -------------------------
    {
        twk_client* c = twk_client_create(&delegates, nullptr);

        std::string result = call(c, 1, "initWalletKit", kInitConfig);
        bool got = contains(result, "\"networks\"");
        printf("%s: initWalletKit\n", got ? "ok" : "FAIL");
        ok = ok && got;

        result = call(c, 2, "createMnemonic", "[]");
        got = std::count(result.begin(), result.end(), ',') == 23; // 24 words
        printf("%s: createMnemonic (24 words)\n", got ? "ok" : "FAIL");
        ok = ok && got;

        first_address = buildWallet(c, ok, "pass 1");

        result = call(c, 13, "getWallets", "[]");
        wallet_id = field(result, "walletId");
        got = !wallet_id.empty();
        printf("%s: getWallets -> walletId %.24s…\n", got ? "ok" : "FAIL", wallet_id.c_str());
        ok = ok && got;

        if (!wallet_id.empty()) {
            // Balance comes from the recorded account state.
            result = call(c, 14, "getBalance", "[\"" + wallet_id + "\"]");
            // Returns the amount itself, as walletkit does — not a wrapper object.
            got = contains(result, "\"result\":\"250000000\"");
            printf("%s: getBalance -> %.90s\n", got ? "ok" : "FAIL", result.c_str());
            ok = ok && got;

            // Build a transfer (recipient must be user-friendly form).
            std::string transfer_params = "[\"" + wallet_id +
                                          "\",{\"recipientAddress\":\"" + first_address +
                                          "\",\"transferAmount\":\"1000000\",\"comment\":\"twk test\"}]";
            result = call(c, 15, "createTransferTonTransaction", transfer_params);
            got = contains(result, "\"messages\"") && contains(result, "\"fromAddress\"");
            printf("%s: createTransferTonTransaction -> %.110s\n", got ? "ok" : "FAIL", result.c_str());
            ok = ok && got;

            // Sign it with a fake signature — shaped correctly, unusable.
            if (got) {
                std::string transaction = result.substr(result.find("{\"messages\""));
                transaction = transaction.substr(0, transaction.rfind('}')); // strip the envelope
                std::string sign_params = "[\"" + wallet_id + "\"," + transaction + ",{\"fakeSignature\":true}]";
                result = call(c, 16, "getSignedSendTransaction", sign_params);
                // The BOC itself: a base64 string, so the envelope is {"result":"te6…"}.
                got = contains(result, "\"result\":\"te6");
                printf("%s: getSignedSendTransaction (fake sig) -> %.100s\n", got ? "ok" : "FAIL",
                       result.c_str());
                ok = ok && got;
            }
        }

        // The host persists the mnemonic itself — walletkit does not.
        result = call(c, 17, "storageSet", "[\"mnemonic\",\"adult maid prison crash\"]");
        got = contains(result, "\"ok\":true");
        printf("%s: host persisted the mnemonic\n", got ? "ok" : "FAIL");
        ok = ok && got;

        twk_client_destroy(c);
    }

    // ---- pass 2: a fresh client restores from storage ----------------------
    {
        twk_client* c = twk_client_create(&delegates, nullptr);

        std::string result = call(c, 1, "initWalletKit", kInitConfig);

        // Wallets do NOT come back by themselves: walletkit keeps them in memory.
        result = call(c, 2, "getWallets", "[]");
        bool got = contains(result, "[]");
        printf("%s: wallets are NOT auto-restored (expected) -> %.60s\n", got ? "ok" : "FAIL", result.c_str());
        ok = ok && got;

        // The mnemonic does come back, through the storage delegate.
        result = call(c, 3, "storageGet", "[\"mnemonic\"]");
        got = contains(result, "adult maid prison crash");
        printf("%s: mnemonic restored from storage\n", got ? "ok" : "FAIL");
        ok = ok && got;

        // Rebuilding from it must yield the same wallet.
        std::string second_address = buildWallet(c, ok, "pass 2");
        got = !second_address.empty() && second_address == first_address;
        printf("%s: rebuilt wallet has the same address (%s)\n", got ? "ok" : "FAIL", second_address.c_str());
        ok = ok && got;

        twk_client_destroy(c);
    }

    {
        std::lock_guard<std::mutex> guard(g_mutex);
        if (!g_unmatched.empty()) {
            printf("note: %zu request(s) had no fixture (answered {}):\n", g_unmatched.size());
            for (size_t i = 0; i < g_unmatched.size() && i < 5; ++i) {
                printf("      %.120s\n", g_unmatched[i].c_str());
            }
        }
    }

    printf(ok ? "PASS\n" : "FAILED\n");
    return ok ? 0 : 1;
}
