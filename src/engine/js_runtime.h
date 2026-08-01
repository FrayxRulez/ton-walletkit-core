//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// ton-walletkit-core — QuickJS runtime/context wrapper (internal).
//
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
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

    // Bound how long one synchronous run of JS may take. Without this a runaway
    // script (`while (true) {}`) wedges the worker thread forever, and the client
    // can never be destroyed. Only synchronous execution is affected: waiting on a
    // promise runs no JS, so ordinary async work is untouched.
    //
    // startDeadline is called before entering JS and clearDeadline after; the
    // interrupt handler aborts execution once the deadline passes, surfacing as a
    // normal JS exception ("interrupted").
    void startDeadline(std::chrono::milliseconds budget);
    void clearDeadline();

    // Default budget for one synchronous run. 60s unless TWK_JS_BUDGET_MS says
    // otherwise (tests use a short one; a host could tighten it).
    std::chrono::milliseconds defaultBudget() const { return budget_; }

private:
    static int interruptHandler(JSRuntime* rt, void* opaque);

    JSRuntime* rt_ = nullptr;
    JSContext* ctx_ = nullptr;

    // steady_clock ticks; 0 means "no deadline".
    std::atomic<int64_t> deadline_{0};
    std::chrono::milliseconds budget_{60000};
};

// RAII guard around a synchronous entry into JS.
class JsDeadline {
public:
    explicit JsDeadline(JsRuntime& js) : js_(js) { js_.startDeadline(js.defaultBudget()); }
    JsDeadline(JsRuntime& js, std::chrono::milliseconds budget) : js_(js) { js_.startDeadline(budget); }
    ~JsDeadline() { js_.clearDeadline(); }

    JsDeadline(const JsDeadline&) = delete;
    JsDeadline& operator=(const JsDeadline&) = delete;

private:
    JsRuntime& js_;
};

} // namespace twk
