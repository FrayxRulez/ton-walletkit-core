// Storage delegate contract: get-missing, set/get, update, remove, clear — driven
// from JS through HostStorageAdapter -> __twk_storage -> the delegate. Also checks
// that data written by one client is visible to the next (file-backed store), the
// property wallet persistence will depend on in M3. Returns 0 on pass.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "reference_host.h"
#include "twk/twk.h"

static bool contains(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

static std::string call(twk_client* c, unsigned long long id, const char* method, const char* params) {
    twk_send(c, id, method, params);
    unsigned long long rid = 0;
    const char* out = twk_receive(c, 60.0, &rid);
    return out ? std::string(out) : std::string();
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);

    // A temp file so the store survives a client, as a real secure store would.
    std::string path;
    if (const char* tmp = std::getenv("TEMP")) {
        path = std::string(tmp) + "\\twk_storage_test.json";
    } else {
        path = "twk_storage_test.json";
    }
    std::remove(path.c_str());

    bool ok = true;

    {
        twk::refhost::ReferenceHost host(path);
        twk_client* c = twk_client_create(host.delegates(), host.userData());

        std::string result = call(c, 1, "storageProbe", "[]");
        bool got = contains(result, "\"available\":true") &&
                   contains(result, "\"missing\":null") &&      // absent key -> null
                   contains(result, "\"first\":\"v1\"") &&      // set/get
                   contains(result, "\"updated\":\"v2\"") &&    // overwrite
                   contains(result, "\"removed\":null") &&      // remove
                   contains(result, "\"afterClear\":null");     // clear
        printf("%s: storage contract -> %.160s\n", got ? "ok" : "FAIL", result.c_str());
        ok = ok && got;

        result = call(c, 2, "storageSet", "[\"persisted-key\",\"persisted-value\"]");
        got = contains(result, "\"ok\":true");
        printf("%s: write for persistence check\n", got ? "ok" : "FAIL");
        ok = ok && got;

        twk_client_destroy(c);
    }

    // A brand-new client (and host) over the same file must see the value.
    {
        twk::refhost::ReferenceHost host(path);
        twk_client* c = twk_client_create(host.delegates(), host.userData());

        std::string result = call(c, 1, "storageGet", "[\"persisted-key\"]");
        bool got = contains(result, "\"value\":\"persisted-value\"");
        printf("%s: survives client restart -> %.100s\n", got ? "ok" : "FAIL", result.c_str());
        ok = ok && got;

        twk_client_destroy(c);
    }

    std::remove(path.c_str());
    printf(ok ? "PASS\n" : "FAILED\n");
    return ok ? 0 : 1;
}
