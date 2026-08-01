//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// twk-bundlec — compile the JS bundle to QuickJS bytecode at build time.
//
// Usage: twk-bundlec <in.js> <out.h> [symbol] [--deflate]
//
// Each client otherwise re-parses ~1.7MB of JS on startup. Precompiling moves the
// parse to build time; the runtime only deserializes (JS_ReadObject) and runs.
//
// --deflate compresses the bytecode before embedding it. Measured on the current
// bundle: 1831 KB of base64 in .rodata becomes 758 KB, and the share of the
// download it accounts for falls from 744 KB to 571 KB — worth having when the
// library ships inside an app with hundreds of millions of installs. The runtime
// pays one inflate of ~1.4MB at load, which is noise beside the ~1.1s the bundle
// takes to execute.
//
// The output is tied to this exact QuickJS build, so the tool is built from the
// same vendored submodule and run as part of the build. When cross-compiling the
// tool cannot run on the build host for the target, so the core keeps the source
// path as a fallback (see TWK_BUNDLE_BYTECODE).
//
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "quickjs.h"
#include "util/base64.h"

#if TWK_BUNDLEC_ZLIB
#include <zlib.h>
#endif

static bool read_file(const char* path, std::string& out) {
    FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(static_cast<size_t>(size));
    size_t got = std::fread(&out[0], 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: twk-bundlec <in.js> <out.h> [symbol] [--deflate]\n");
        return 2;
    }
    const char* in_path = argv[1];
    const char* out_path = argv[2];
    const char* symbol = "twk_bundle_bc";
    bool deflate_payload = false;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--deflate") == 0) {
            deflate_payload = true;
        } else {
            symbol = argv[i];
        }
    }

#if !TWK_BUNDLEC_ZLIB
    if (deflate_payload) {
        // Loud rather than quietly emitting a bundle the core will try to
        // inflate: the target asked for compression, so this host tool is the
        // wrong one to have been handed to it.
        std::fprintf(stderr, "twk-bundlec: --deflate requested but this build has no zlib.\n"
                             "  Rebuild the host tool where zlib is available, or configure the\n"
                             "  target with -DTWK_COMPRESS_BUNDLE=OFF.\n");
        return 2;
    }
#endif

    std::string source;
    if (!read_file(in_path, source)) {
        std::fprintf(stderr, "twk-bundlec: cannot read %s\n", in_path);
        return 1;
    }

    JSRuntime* rt = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(rt);

    // Compile only: produces the global-code function object without running it
    // (the bundle references host globals that only exist at runtime).
    JSValue fn = JS_Eval(ctx, source.c_str(), source.size(), "<bundle>",
                         JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(fn)) {
        JSValue exc = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, exc);
        std::fprintf(stderr, "twk-bundlec: compile failed: %s\n", msg ? msg : "(unknown)");
        if (msg != nullptr) {
            JS_FreeCString(ctx, msg);
        }
        JS_FreeValue(ctx, exc);
        JS_FreeValue(ctx, fn);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }

    size_t bc_len = 0;
    uint8_t* bc = JS_WriteObject(ctx, &bc_len, fn,
                                 JS_WRITE_OBJ_BYTECODE | JS_WRITE_OBJ_STRIP_SOURCE | JS_WRITE_OBJ_STRIP_DEBUG);
    JS_FreeValue(ctx, fn);
    if (bc == nullptr) {
        std::fprintf(stderr, "twk-bundlec: JS_WriteObject failed\n");
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }

    const size_t raw_len = bc_len;
    std::vector<uint8_t> payload(bc, bc + bc_len);
    js_free(ctx, bc);

#if TWK_BUNDLEC_ZLIB
    if (deflate_payload) {
        uLongf bound = compressBound(static_cast<uLong>(payload.size()));
        std::vector<uint8_t> deflated(bound);
        const int rc = compress2(deflated.data(), &bound, payload.data(),
                                 static_cast<uLong>(payload.size()), Z_BEST_COMPRESSION);
        if (rc != Z_OK) {
            std::fprintf(stderr, "twk-bundlec: deflate failed (%d)\n", rc);
            JS_FreeContext(ctx);
            JS_FreeRuntime(rt);
            return 1;
        }
        deflated.resize(bound);
        payload.swap(deflated);
    }
#endif

    // Emit as base64 string literals: fast for the compiler to ingest, unlike a
    // multi-MB byte-array initializer.
    std::string b64 = twk::base64::encode(payload.data(), payload.size());

    FILE* out = std::fopen(out_path, "wb");
    if (out == nullptr) {
        std::fprintf(stderr, "twk-bundlec: cannot write %s\n", out_path);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }
    std::fprintf(out, "// Generated by twk-bundlec from %s. Do not edit.\n", in_path);
    // The core switches on this rather than on a build option, so the header and
    // the code that reads it can never disagree about what is embedded.
    std::fprintf(out, "#define TWK_BUNDLE_BC_DEFLATED %d\n", deflate_payload ? 1 : 0);
    std::fprintf(out, "static const char %s[] =\n", symbol);
    const size_t kChunk = 20000; // under the MSVC 65535-byte string-literal limit
    for (size_t i = 0; i < b64.size(); i += kChunk) {
        std::fprintf(out, "\"%s\"\n", b64.substr(i, kChunk).c_str());
    }
    std::fprintf(out, ";\nstatic const unsigned long %s_len = %luul;\n", symbol,
                 static_cast<unsigned long>(b64.size()));
    // The inflated size, so the reader allocates once instead of growing.
    std::fprintf(out, "static const unsigned long %s_raw_len = %luul;\n", symbol,
                 static_cast<unsigned long>(raw_len));
    std::fclose(out);

    std::printf("twk-bundlec: %s (%zu bytes JS) -> %s (%zu bytes bytecode%s, %zu bytes embedded)\n",
                in_path, source.size(), out_path, raw_len, deflate_payload ? ", deflated" : "", b64.size());

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return 0;
}
