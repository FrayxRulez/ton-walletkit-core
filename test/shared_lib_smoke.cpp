// Loads twk.dll the way a binding does — dynamically, by name, with no link-time
// dependency — and drives a full request through the resolved pointers. This is
// the closest C++ analogue of what P/Invoke will do, so it catches export,
// calling-convention and packaging problems before the C# layer exists.
#include <cstdio>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

// Mirrors include/twk/twk.h without including it: the point is to resolve the ABI
// exactly as a foreign runtime would.
using twk_client = void;
using create_fn = twk_client* (*)(const void*, void*);
using destroy_fn = void (*)(twk_client*);
using send_fn = void (*)(twk_client*, unsigned long long, const char*, const char*);
using receive_fn = const char* (*)(twk_client*, double, unsigned long long*);

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);

#if !defined(_WIN32)
    printf("SKIP: dynamic-load smoke is Windows-only for now\n");
    return 0;
#else
    HMODULE lib = LoadLibraryA("twk.dll");
    if (lib == nullptr) {
        printf("FAIL: LoadLibrary(twk.dll) failed (%lu)\n", GetLastError());
        return 1;
    }

    auto create = reinterpret_cast<create_fn>(GetProcAddress(lib, "twk_client_create"));
    auto destroy = reinterpret_cast<destroy_fn>(GetProcAddress(lib, "twk_client_destroy"));
    auto send = reinterpret_cast<send_fn>(GetProcAddress(lib, "twk_send"));
    auto receive = reinterpret_cast<receive_fn>(GetProcAddress(lib, "twk_receive"));

    bool resolved = create && destroy && send && receive;
    printf("%s: resolved the ABI by name (undecorated C exports)\n", resolved ? "ok" : "FAIL");
    if (!resolved) {
        FreeLibrary(lib);
        return 1;
    }

    // Also confirm the delegate-completion entry points are exported, since the
    // binding must call them from its own threads.
    bool completions = GetProcAddress(lib, "twk_http_respond") && GetProcAddress(lib, "twk_http_failed") &&
                       GetProcAddress(lib, "twk_storage_respond") && GetProcAddress(lib, "twk_sse_event") &&
                       GetProcAddress(lib, "twk_sse_closed");
    printf("%s: completion entry points exported\n", completions ? "ok" : "FAIL");

    twk_client* c = create(nullptr, nullptr);
    send(c, 7, "echo", "[\"through the dll\"]");

    unsigned long long rid = 0;
    const char* out = receive(c, 60.0, &rid);
    std::string result = out ? out : "";
    bool round_trip = rid == 7 && result.find("through the dll") != std::string::npos;
    printf("%s: request round-tripped through the DLL -> %.80s\n", round_trip ? "ok" : "FAIL",
           result.c_str());

    destroy(c);
    FreeLibrary(lib);

    bool ok = resolved && completions && round_trip;
    printf(ok ? "PASS\n" : "FAILED\n");
    return ok ? 0 : 1;
#endif
}
