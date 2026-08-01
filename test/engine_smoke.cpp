//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// Sanity check for the JsRuntime wrapper: register a native global, eval JS that
// calls it, and read the result back as JSON. Returns 0 iff the result is 42.
#include <cstdio>
#include <string>

#include "engine/js_runtime.h"

using namespace twk;

static JSValue nativeAdd(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    int32_t a = 0, b = 0;
    if (argc > 0) {
        JS_ToInt32(ctx, &a, argv[0]);
    }
    if (argc > 1) {
        JS_ToInt32(ctx, &b, argv[1]);
    }
    return JS_NewInt32(ctx, a + b);
}

int main() {
    JsRuntime js;
    js.registerGlobal("nativeAdd", nativeAdd, 2);

    std::string result, error;
    if (!js.eval("nativeAdd(20, 22)", "<engine_smoke>", &result, &error)) {
        printf("eval failed: %s\n", error.c_str());
        return 1;
    }

    printf("engine eval nativeAdd(20, 22) = %s\n", result.c_str());

    // Also confirm the exception path renders something non-empty.
    std::string err2;
    if (js.eval("throw new Error('boom')", "<engine_smoke>", nullptr, &err2)) {
        printf("expected exception did not fire\n");
        return 1;
    }
    printf("exception path: %s\n", err2.c_str());

    return result == "42" && !err2.empty() ? 0 : 1;
}
