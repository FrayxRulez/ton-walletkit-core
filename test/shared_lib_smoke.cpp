//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// Loads the shared library the way a binding does — dynamically, by name, with no
// link-time dependency — and drives a full request through the resolved pointers.
// This is the closest C++ analogue of what P/Invoke, JNI or dlopen-from-Swift
// will do, so it catches export, calling-convention and packaging problems before
// any binding exists.
#include <cstdio>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

// Mirrors include/twk/twk.h without including it: the point is to resolve the ABI
// exactly as a foreign runtime would.
using twk_client = void;
using create_fn = twk_client* (*)(const void*, void*);
using destroy_fn = void (*)(twk_client*);
using send_fn = void (*)(twk_client*, unsigned long long, const char*, const char*);
using receive_fn = const char* (*)(twk_client*, double, unsigned long long*);

// The platform's own name for the same library, and its own loader.
#if defined(_WIN32)
const char* kLibrary = "twk.dll";
using Library = HMODULE;

Library load() {
    return LoadLibraryA(kLibrary);
}
void* symbol(Library lib, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(lib, name));
}
void unload(Library lib) {
    FreeLibrary(lib);
}
std::string last_error() {
    return "error " + std::to_string(GetLastError());
}
#else
#if defined(__APPLE__)
const char* kLibrary = "libtwk.dylib";
#else
const char* kLibrary = "libtwk.so";
#endif
using Library = void*;

Library load() {
    // RTLD_LOCAL, as a binding's own loader would: the symbols must come from the
    // handle, not leak into the global namespace.
    return dlopen(kLibrary, RTLD_NOW | RTLD_LOCAL);
}
void* symbol(Library lib, const char* name) {
    return dlsym(lib, name);
}
void unload(Library lib) {
    dlclose(lib);
}
std::string last_error() {
    const char* message = dlerror();
    return message != nullptr ? message : "unknown error";
}
#endif

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);

    Library lib = load();
    if (lib == nullptr) {
        // The library sits next to this executable; the loader looks there on
        // Windows but not necessarily elsewhere, so this is a setup failure
        // rather than an ABI one.
        printf("SKIP: could not load %s (%s)\n", kLibrary, last_error().c_str());
        return 0;
    }

    auto create = reinterpret_cast<create_fn>(symbol(lib, "twk_client_create"));
    auto destroy = reinterpret_cast<destroy_fn>(symbol(lib, "twk_client_destroy"));
    auto send = reinterpret_cast<send_fn>(symbol(lib, "twk_send"));
    auto receive = reinterpret_cast<receive_fn>(symbol(lib, "twk_receive"));

    bool resolved = create && destroy && send && receive;
    printf("%s: resolved the ABI by name (undecorated C exports)\n", resolved ? "ok" : "FAIL");
    if (!resolved) {
        unload(lib);
        return 1;
    }

    // Also confirm the delegate-completion entry points are exported, since the
    // binding must call them from its own threads.
    bool completions = symbol(lib, "twk_http_respond") && symbol(lib, "twk_http_failed") &&
                       symbol(lib, "twk_storage_respond") && symbol(lib, "twk_sse_event") &&
                       symbol(lib, "twk_sse_closed");
    printf("%s: completion entry points exported\n", completions ? "ok" : "FAIL");

    twk_client* c = create(nullptr, nullptr);
    send(c, 7, "echo", "[\"through the shared library\"]");

    unsigned long long rid = 0;
    const char* out = receive(c, 60.0, &rid);
    std::string result = out ? out : "";
    bool round_trip = rid == 7 && result.find("through the shared library") != std::string::npos;
    printf("%s: request round-tripped through the shared library -> %.80s\n", round_trip ? "ok" : "FAIL",
           result.c_str());

    destroy(c);
    unload(lib);

    bool ok = resolved && completions && round_trip;
    printf(ok ? "PASS\n" : "FAILED\n");
    return ok ? 0 : 1;
}
