//
// ton-walletkit-core — host shims implementation (internal).
//
#include "shims/shims.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "client.h"
#include "engine/event_loop.h"
#include "engine/js_runtime.h"
#include "host_context.h"
#include "util/base64.h"
#include "util/crypto.h"

namespace twk {

namespace {

// ---- native shim functions ------------------------------------------------

Shims* shims_of(JSContext* ctx) {
    auto* hc = static_cast<HostContext*>(JS_GetContextOpaque(ctx));
    return hc != nullptr ? hc->shims : nullptr;
}

// console.* — stringify each arg and print to stderr. Level is not distinguished.
JSValue js_console(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    std::string line;
    for (int i = 0; i < argc; ++i) {
        if (i > 0) {
            line += ' ';
        }
        const char* s = JS_ToCString(ctx, argv[i]);
        if (s != nullptr) {
            line += s;
            JS_FreeCString(ctx, s);
        }
    }
    std::fprintf(stderr, "[js] %s\n", line.c_str());
    return JS_UNDEFINED;
}

// crypto.getRandomValues(uint8Array) -> the same array, filled with CSPRNG bytes.
JSValue js_get_random_values(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_UNDEFINED;
    }
    size_t size = 0;
    uint8_t* ptr = JS_GetUint8Array(ctx, &size, argv[0]);
    if (ptr == nullptr) {
        return JS_ThrowTypeError(ctx, "getRandomValues expects a Uint8Array");
    }
    if (!crypto::random_bytes(ptr, size)) {
        return JS_ThrowInternalError(ctx, "getRandomValues: RNG failure");
    }
    return JS_DupValue(ctx, argv[0]);
}

// ---- __twk_crypto: native hashing over typed arrays ------------------------
// Uint8Array in / Uint8Array out (no base64) — the JS crypto-primitives polyfill
// calls these; it falls back to pure JS when __twk_crypto is absent.

// Hash-style: fn(Uint8Array) -> Uint8Array
template <bool (*Fn)(const uint8_t*, size_t, uint8_t*), size_t OutLen>
JSValue js_hash(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    size_t len = 0;
    uint8_t* data = argc > 0 ? JS_GetUint8Array(ctx, &len, argv[0]) : nullptr;
    if (data == nullptr && len != 0) {
        return JS_ThrowTypeError(ctx, "expected a Uint8Array");
    }
    uint8_t out[OutLen];
    if (!Fn(data, len, out)) {
        return JS_ThrowInternalError(ctx, "hash failure");
    }
    return JS_NewUint8ArrayCopy(ctx, out, OutLen);
}

// hmacSha512(key: Uint8Array, data: Uint8Array) -> Uint8Array(64)
JSValue js_hmac_sha512(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "hmacSha512 expects (key, data)");
    }
    size_t key_len = 0, data_len = 0;
    uint8_t* key = JS_GetUint8Array(ctx, &key_len, argv[0]);
    uint8_t* data = JS_GetUint8Array(ctx, &data_len, argv[1]);
    if ((key == nullptr && key_len != 0) || (data == nullptr && data_len != 0)) {
        return JS_ThrowTypeError(ctx, "hmacSha512 expects Uint8Arrays");
    }
    uint8_t out[64];
    if (!crypto::hmac_sha512(key, key_len, data, data_len, out)) {
        return JS_ThrowInternalError(ctx, "hmacSha512 failure");
    }
    return JS_NewUint8ArrayCopy(ctx, out, 64);
}

// pbkdf2Sha512(password: Uint8Array, salt: Uint8Array, iterations, keyLen) -> Uint8Array
JSValue js_pbkdf2(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 4) {
        return JS_ThrowTypeError(ctx, "pbkdf2Sha512 expects (password, salt, iterations, keyLen)");
    }
    size_t pw_len = 0, salt_len = 0;
    uint8_t* pw = JS_GetUint8Array(ctx, &pw_len, argv[0]);
    uint8_t* salt = JS_GetUint8Array(ctx, &salt_len, argv[1]);
    int64_t iterations = 0, key_len = 0;
    JS_ToInt64(ctx, &iterations, argv[2]);
    JS_ToInt64(ctx, &key_len, argv[3]);
    if (iterations <= 0 || key_len <= 0 || key_len > (1 << 20)) {
        return JS_ThrowRangeError(ctx, "pbkdf2Sha512: bad iterations/keyLen");
    }

    std::vector<uint8_t> out(static_cast<size_t>(key_len));
    if (!crypto::pbkdf2_sha512(pw, pw_len, salt, salt_len, static_cast<uint32_t>(iterations), out.data(),
                               out.size())) {
        return JS_ThrowInternalError(ctx, "pbkdf2Sha512 failure");
    }
    return JS_NewUint8ArrayCopy(ctx, out.data(), out.size());
}

// Pbkdf2.derive(passwordB64, saltB64, iterations, keyLen, "sha-512") -> base64 string.
JSValue js_pbkdf2_derive(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 5) {
        return JS_ThrowTypeError(ctx, "Pbkdf2.derive expects 5 arguments");
    }

    const char* hash = JS_ToCString(ctx, argv[4]);
    bool sha512 = hash != nullptr && (std::strcmp(hash, "sha-512") == 0 || std::strcmp(hash, "SHA-512") == 0);
    if (hash != nullptr) {
        JS_FreeCString(ctx, hash);
    }
    if (!sha512) {
        return JS_ThrowTypeError(ctx, "Pbkdf2.derive: only sha-512 is supported");
    }

    size_t pw_len = 0, salt_len = 0;
    const char* pw = JS_ToCStringLen(ctx, &pw_len, argv[0]);
    const char* salt = JS_ToCStringLen(ctx, &salt_len, argv[1]);
    int64_t iterations = 0, key_len = 0;
    JS_ToInt64(ctx, &iterations, argv[2]);
    JS_ToInt64(ctx, &key_len, argv[3]);

    std::vector<uint8_t> pw_bytes = pw != nullptr ? base64::decode(pw, pw_len) : std::vector<uint8_t>{};
    std::vector<uint8_t> salt_bytes = salt != nullptr ? base64::decode(salt, salt_len) : std::vector<uint8_t>{};
    if (pw != nullptr) {
        JS_FreeCString(ctx, pw);
    }
    if (salt != nullptr) {
        JS_FreeCString(ctx, salt);
    }

    if (key_len <= 0 || iterations <= 0) {
        return JS_ThrowRangeError(ctx, "Pbkdf2.derive: bad iterations/keyLen");
    }

    std::vector<uint8_t> out(static_cast<size_t>(key_len));
    if (!crypto::pbkdf2_sha512(pw_bytes.data(), pw_bytes.size(), salt_bytes.data(), salt_bytes.size(),
                               static_cast<uint32_t>(iterations), out.data(), out.size())) {
        return JS_ThrowInternalError(ctx, "Pbkdf2.derive: PBKDF2 failure");
    }

    std::string b64 = base64::encode(out.data(), out.size());
    return JS_NewStringLen(ctx, b64.data(), b64.size());
}

// ---- __twk_http: the primitive the JS fetch shim is built on ---------------
// __twk_http(method, url, headersJson, bodyOrNull, cb) -> token
//   cb(error | null, status, headersJson, body) runs on the worker thread.
// Callbacks (not promises) keep the C++ side simple; the JS shim wraps this in a
// Promise and layers Headers/Response/AbortController on top.
JSValue js_http(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    auto* hc = static_cast<HostContext*>(JS_GetContextOpaque(ctx));
    Client* client = hc != nullptr ? hc->client : nullptr;
    if (client == nullptr) {
        return JS_ThrowInternalError(ctx, "__twk_http: no client");
    }
    if (argc < 5 || !JS_IsFunction(ctx, argv[4])) {
        return JS_ThrowTypeError(ctx, "__twk_http(method, url, headersJson, body, cb)");
    }

    auto str = [&](JSValueConst v) -> std::string {
        if (JS_IsUndefined(v) || JS_IsNull(v)) {
            return {};
        }
        const char* s = JS_ToCString(ctx, v);
        if (s == nullptr) {
            return {};
        }
        std::string out(s);
        JS_FreeCString(ctx, s);
        return out;
    };

    std::string method = str(argv[0]);
    std::string url = str(argv[1]);
    std::string headers = str(argv[2]);
    std::string body = str(argv[3]);

    // Owns a reference to the JS callback and releases it however the handler
    // ends — invoked, cancelled, or dropped at teardown. Client::onStop clears
    // pending handlers while the context is still alive, so this is safe.
    struct CallbackRef {
        JSContext* ctx;
        JSValue fn;
        CallbackRef(JSContext* c, JSValue f) : ctx(c), fn(f) {}
        // Non-copyable: a copy would double-free the reference (and constructing
        // from a temporary would free it immediately).
        CallbackRef(const CallbackRef&) = delete;
        CallbackRef& operator=(const CallbackRef&) = delete;
        ~CallbackRef() { JS_FreeValue(ctx, fn); }
    };
    auto cb = std::make_shared<CallbackRef>(ctx, JS_DupValue(ctx, argv[4]));

    int64_t token = client->startHttp(method, url, headers, body, [client, cb](HttpResult result) {
        JSContext* c = client->js()->context();
        JSValue args[4];
        args[0] = result.ok ? JS_NULL : JS_NewString(c, result.error.c_str());
        args[1] = JS_NewInt32(c, result.status);
        args[2] = JS_NewString(c, result.headers_json.c_str());
        args[3] = JS_NewString(c, result.body.c_str());

        JSValue global = JS_GetGlobalObject(c);
        JSValue ret = JS_Call(c, cb->fn, global, 4, args);
        if (JS_IsException(ret)) {
            std::fprintf(stderr, "[http] uncaught: %s\n", client->js()->takeExceptionText().c_str());
        }

        JS_FreeValue(c, ret);
        JS_FreeValue(c, global);
        for (JSValue& arg : args) {
            JS_FreeValue(c, arg);
        }
    });

    return JS_NewInt64(ctx, token);
}

JSValue js_http_cancel(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    auto* hc = static_cast<HostContext*>(JS_GetContextOpaque(ctx));
    Client* client = hc != nullptr ? hc->client : nullptr;
    if (client == nullptr || argc < 1) {
        return JS_UNDEFINED;
    }
    int64_t token = 0;
    JS_ToInt64(ctx, &token, argv[0]);
    client->cancelHttp(token);
    return JS_UNDEFINED;
}

JSValue js_set_timer(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv, bool repeat) {
    Shims* shims = shims_of(ctx);
    if (shims == nullptr || argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_NewInt64(ctx, 0);
    }
    int64_t delay = 0;
    if (argc >= 2) {
        JS_ToInt64(ctx, &delay, argv[1]);
    }
    uint64_t id = shims->addTimer(argv[0], delay < 0 ? 0 : delay, repeat);
    return JS_NewInt64(ctx, static_cast<int64_t>(id));
}

JSValue js_set_timeout(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return js_set_timer(ctx, this_val, argc, argv, /*repeat=*/false);
}

JSValue js_set_interval(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return js_set_timer(ctx, this_val, argc, argv, /*repeat=*/true);
}

JSValue js_clear_timer(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    Shims* shims = shims_of(ctx);
    if (shims == nullptr || argc < 1) {
        return JS_UNDEFINED;
    }
    int64_t id = 0;
    JS_ToInt64(ctx, &id, argv[0]);
    shims->clearTimer(static_cast<uint64_t>(id));
    return JS_UNDEFINED;
}

} // namespace

// ---- Shims ----------------------------------------------------------------

Shims::Shims(JsRuntime& js, EventLoop& loop) : js_(js), loop_(loop) {}

Shims::~Shims() {
    JSContext* ctx = js_.context();
    for (auto& entry : timers_) {
        JS_FreeValue(ctx, entry.second.fn);
    }
    timers_.clear();
}

void Shims::install() {
    JSContext* ctx = js_.context();
    JSValue global = JS_GetGlobalObject(ctx);

    // console.*
    JSValue console = JS_NewObject(ctx);
    for (const char* name : {"log", "info", "warn", "error", "debug", "trace"}) {
        JS_SetPropertyStr(ctx, console, name, JS_NewCFunction(ctx, js_console, name, 1));
    }
    JS_SetPropertyStr(ctx, global, "console", console);

    // crypto.getRandomValues (create crypto if absent; generic.ts adds randomUUID later)
    JSValue crypto = JS_GetPropertyStr(ctx, global, "crypto");
    if (!JS_IsObject(crypto)) {
        JS_FreeValue(ctx, crypto);
        crypto = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, global, "crypto", JS_DupValue(ctx, crypto));
    }
    JS_SetPropertyStr(ctx, crypto, "getRandomValues", JS_NewCFunction(ctx, js_get_random_values, "getRandomValues", 1));
    JS_FreeValue(ctx, crypto);

    // Pbkdf2.derive — the kit-ios contract (base64 in/out), used by the
    // react-native-fast-pbkdf2 polyfill.
    JSValue pbkdf2 = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, pbkdf2, "derive", JS_NewCFunction(ctx, js_pbkdf2_derive, "derive", 5));
    JS_SetPropertyStr(ctx, global, "Pbkdf2", pbkdf2);

    // __twk_crypto — native hashing over typed arrays (our own, faster contract).
    // Only published when the platform backend works, so the JS polyfill can
    // detect its absence and fall back to pure JS.
    if (crypto::available()) {
        JSValue nc = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, nc, "sha256", JS_NewCFunction(ctx, (js_hash<crypto::sha256, 32>), "sha256", 1));
        JS_SetPropertyStr(ctx, nc, "sha512", JS_NewCFunction(ctx, (js_hash<crypto::sha512, 64>), "sha512", 1));
        JS_SetPropertyStr(ctx, nc, "hmacSha512", JS_NewCFunction(ctx, js_hmac_sha512, "hmacSha512", 2));
        JS_SetPropertyStr(ctx, nc, "pbkdf2Sha512", JS_NewCFunction(ctx, js_pbkdf2, "pbkdf2Sha512", 4));
        JS_SetPropertyStr(ctx, global, "__twk_crypto", nc);
    }

    // HTTP primitive (the JS fetch shim builds Headers/Response/fetch on this).
    JS_SetPropertyStr(ctx, global, "__twk_http", JS_NewCFunction(ctx, js_http, "__twk_http", 5));
    JS_SetPropertyStr(ctx, global, "__twk_http_cancel",
                      JS_NewCFunction(ctx, js_http_cancel, "__twk_http_cancel", 1));

    // timers
    JS_SetPropertyStr(ctx, global, "setTimeout", JS_NewCFunction(ctx, js_set_timeout, "setTimeout", 2));
    JS_SetPropertyStr(ctx, global, "setInterval", JS_NewCFunction(ctx, js_set_interval, "setInterval", 2));
    JS_SetPropertyStr(ctx, global, "clearTimeout", JS_NewCFunction(ctx, js_clear_timer, "clearTimeout", 1));
    JS_SetPropertyStr(ctx, global, "clearInterval", JS_NewCFunction(ctx, js_clear_timer, "clearInterval", 1));

    JS_FreeValue(ctx, global);
}

uint64_t Shims::addTimer(JSValueConst fn, int64_t delay_ms, bool repeat) {
    JSContext* ctx = js_.context();
    uint64_t id = next_timer_id_++;
    JSValue dup = JS_DupValue(ctx, fn);
    uint64_t loop_id = loop_.addTimer(delay_ms, repeat, [this, id] { fireTimer(id); });
    timers_[id] = TimerEntry{dup, repeat, loop_id};
    return id;
}

void Shims::clearTimer(uint64_t id) {
    auto it = timers_.find(id);
    if (it == timers_.end()) {
        return;
    }
    loop_.clearTimer(it->second.loop_id);
    JS_FreeValue(js_.context(), it->second.fn);
    timers_.erase(it);
}

void Shims::fireTimer(uint64_t id) {
    auto it = timers_.find(id);
    if (it == timers_.end()) {
        return;
    }
    JSContext* ctx = js_.context();
    bool repeat = it->second.repeat;
    JSValue fn = JS_DupValue(ctx, it->second.fn); // keep alive across the call (callback may clear it)

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue ret = JS_Call(ctx, fn, global, 0, nullptr);
    if (JS_IsException(ret)) {
        std::string err = js_.takeExceptionText();
        std::fprintf(stderr, "[timer] uncaught: %s\n", err.c_str());
    }
    JS_FreeValue(ctx, ret);
    JS_FreeValue(ctx, global);
    JS_FreeValue(ctx, fn);

    if (!repeat) {
        auto jt = timers_.find(id);
        if (jt != timers_.end()) {
            JS_FreeValue(ctx, jt->second.fn);
            timers_.erase(jt);
        }
    }
}

} // namespace twk
