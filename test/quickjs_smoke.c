//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// Minimal sanity check that the vendored QuickJS-ng links and evaluates JS.
// Built as `twk_qjs_smoke` when TWK_BUILD_TESTS is ON; returns 0 iff eval == 7.
#include <stdio.h>
#include <string.h>

#include "quickjs.h"

int main(void) {
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);

    const char *code = "1 + 2 * 3";
    JSValue v = JS_Eval(ctx, code, strlen(code), "<smoke>", JS_EVAL_TYPE_GLOBAL);

    int32_t out = -1;
    JS_ToInt32(ctx, &out, v);
    printf("QuickJS-ng eval(\"%s\") = %d\n", code, out);

    JS_FreeValue(ctx, v);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return out == 7 ? 0 : 1;
}
