//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// ton-walletkit-core — QuickJS runtime/context wrapper (internal).
//
#include "engine/js_runtime.h"

#include <cstdlib>

namespace twk {

namespace {

// JSValue -> std::string via JS_ToCString, then free the C string.
std::string cstr(JSContext* ctx, JSValueConst v) {
    const char* s = JS_ToCString(ctx, v);
    if (!s) {
        return {};
    }
    std::string out(s);
    JS_FreeCString(ctx, s);
    return out;
}

} // namespace

namespace {
// Generous enough for the ~2MB bundle plus wallet working set, small enough that
// a runaway allocation fails instead of exhausting the process.
constexpr size_t kMemoryLimit = 512u * 1024u * 1024u;
constexpr size_t kMaxStackSize = 4u * 1024u * 1024u;
} // namespace

JsRuntime::JsRuntime() {
    rt_ = JS_NewRuntime();
    JS_SetMemoryLimit(rt_, kMemoryLimit);
    JS_SetMaxStackSize(rt_, kMaxStackSize);
    JS_SetInterruptHandler(rt_, &JsRuntime::interruptHandler, this);

    // Tests (and hosts) can tighten the runaway-script budget.
    if (const char* env = std::getenv("TWK_JS_BUDGET_MS")) {
        long ms = std::atol(env);
        if (ms > 0) {
            budget_ = std::chrono::milliseconds(ms);
        }
    }
    ctx_ = JS_NewContext(rt_);
}

// Runs periodically during synchronous JS execution; non-zero aborts it.
int JsRuntime::interruptHandler(JSRuntime* /*rt*/, void* opaque) {
    auto* self = static_cast<JsRuntime*>(opaque);
    int64_t deadline = self->deadline_.load(std::memory_order_relaxed);
    if (deadline == 0) {
        return 0; // no budget in force
    }
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return now > deadline ? 1 : 0;
}

void JsRuntime::startDeadline(std::chrono::milliseconds budget) {
    auto at = std::chrono::steady_clock::now() + budget;
    deadline_.store(at.time_since_epoch().count(), std::memory_order_relaxed);
}

void JsRuntime::clearDeadline() {
    deadline_.store(0, std::memory_order_relaxed);
}

JsRuntime::~JsRuntime() {
    if (ctx_) {
        JS_FreeContext(ctx_);
        ctx_ = nullptr;
    }
    if (rt_) {
        JS_FreeRuntime(rt_);
        rt_ = nullptr;
    }
}

bool JsRuntime::eval(const std::string& code, const std::string& filename, std::string* result_json,
                     std::string* error) {
    JSValue val = JS_Eval(ctx_, code.c_str(), code.size(), filename.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(val)) {
        JS_FreeValue(ctx_, val);
        if (error) {
            *error = takeExceptionText();
        }
        return false;
    }
    if (result_json) {
        *result_json = JS_IsUndefined(val) ? std::string() : toJson(val);
    }
    JS_FreeValue(ctx_, val);
    return true;
}

bool JsRuntime::evalBytecode(const uint8_t* data, size_t len, std::string* error) {
    JSValue fn = JS_ReadObject(ctx_, data, len, JS_READ_OBJ_BYTECODE);
    if (JS_IsException(fn)) {
        JS_FreeValue(ctx_, fn);
        if (error) {
            *error = takeExceptionText();
        }
        return false;
    }

    JSValue result = JS_EvalFunction(ctx_, fn); // takes ownership of fn
    if (JS_IsException(result)) {
        JS_FreeValue(ctx_, result);
        if (error) {
            *error = takeExceptionText();
        }
        return false;
    }
    JS_FreeValue(ctx_, result);
    return true;
}

void JsRuntime::registerGlobal(const char* name, JSCFunction* fn, int argc) {
    JSValue global = JS_GetGlobalObject(ctx_);
    JS_SetPropertyStr(ctx_, global, name, JS_NewCFunction(ctx_, fn, name, argc));
    JS_FreeValue(ctx_, global);
}

std::string JsRuntime::toJson(JSValueConst v) {
    JSValue json = JS_JSONStringify(ctx_, v, JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(json) || JS_IsUndefined(json)) {
        JS_FreeValue(ctx_, json);
        return {};
    }
    std::string out = cstr(ctx_, json);
    JS_FreeValue(ctx_, json);
    return out;
}

JSValue JsRuntime::parseJson(const std::string& json) {
    return JS_ParseJSON(ctx_, json.c_str(), json.size(), "<json>");
}

std::string JsRuntime::takeExceptionText() {
    JSValue exc = JS_GetException(ctx_);
    std::string out = cstr(ctx_, exc);

    // Append the stack trace when the exception is an Error object with one.
    if (JS_IsError(exc)) {
        JSValue stack = JS_GetPropertyStr(ctx_, exc, "stack");
        if (!JS_IsUndefined(stack)) {
            std::string s = cstr(ctx_, stack);
            if (!s.empty()) {
                out += "\n";
                out += s;
            }
        }
        JS_FreeValue(ctx_, stack);
    }

    JS_FreeValue(ctx_, exc);
    return out;
}

} // namespace twk
