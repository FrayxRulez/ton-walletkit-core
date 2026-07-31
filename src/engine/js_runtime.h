//
// ton-walletkit-core — QuickJS runtime/context wrapper (internal).
//
#pragma once

#include <string>

#include "quickjs.h"

namespace twk {

// RAII wrapper over a single QuickJS runtime + context.
//
// Not thread-safe: a JsRuntime is owned by exactly one thread (the client's
// worker thread, see event_loop). It is the low-level substrate the bridge
// transport and shims build on — it knows nothing about walletkit.
class JsRuntime {
public:
    JsRuntime();
    ~JsRuntime();

    JsRuntime(const JsRuntime&) = delete;
    JsRuntime& operator=(const JsRuntime&) = delete;

    JSContext* context() const { return ctx_; }
    JSRuntime* runtime() const { return rt_; }

    // Evaluate a global script. Returns true on success.
    // On success, if `result_json` is non-null it receives JSON.stringify(result)
    // ("" when the result is undefined). On failure, `error` (if non-null) receives
    // the exception text.
    bool eval(const std::string& code, const std::string& filename, std::string* result_json, std::string* error);

    // Run precompiled QuickJS bytecode (produced by twk-bundlec). Returns true on
    // success; on failure `error` (if non-null) receives the exception text.
    bool evalBytecode(const uint8_t* data, size_t len, std::string* error);

    // globalThis[name] = <native function>.
    void registerGlobal(const char* name, JSCFunction* fn, int argc);

    // Opaque pointer available to JSCFunction callbacks via JS_GetContextOpaque(ctx).
    void setOpaque(void* opaque) { JS_SetContextOpaque(ctx_, opaque); }

    // JSON.stringify(v) -> string ("" if undefined/exception). Does not free v.
    std::string toJson(JSValueConst v);

    // JSON.parse(json) -> value (JS_EXCEPTION on parse error). Caller owns the result.
    JSValue parseJson(const std::string& json);

    // Take the pending exception and render it (message + stack) to a string.
    std::string takeExceptionText();

private:
    JSRuntime* rt_ = nullptr;
    JSContext* ctx_ = nullptr;
};

} // namespace twk
