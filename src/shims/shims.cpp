//
// ton-walletkit-core — host shims implementation (internal).
//
#include "shims/shims.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/event_loop.h"
#include "engine/js_runtime.h"
#include "host_context.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <bcrypt.h>
#else
#include <random>
#endif

namespace twk {

namespace {

// ---- platform crypto ------------------------------------------------------

bool platform_random(uint8_t* buf, size_t len) {
    if (len == 0) {
        return true;
    }
#if defined(_WIN32)
    return BCryptGenRandom(nullptr, buf, static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    std::random_device rd;
    for (size_t i = 0; i < len; ++i) {
        buf[i] = static_cast<uint8_t>(rd() & 0xff);
    }
    return true;
#endif
}

// PBKDF2-HMAC-SHA512. Returns false if unsupported on this platform.
bool platform_pbkdf2_sha512(const std::vector<uint8_t>& password, const std::vector<uint8_t>& salt,
                            uint32_t iterations, std::vector<uint8_t>& out) {
    if (out.empty()) {
        return true;
    }
#if defined(_WIN32)
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA512_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0) {
        return false;
    }
    NTSTATUS status = BCryptDeriveKeyPBKDF2(
        alg, const_cast<PUCHAR>(password.data()), static_cast<ULONG>(password.size()),
        const_cast<PUCHAR>(salt.data()), static_cast<ULONG>(salt.size()), iterations, out.data(),
        static_cast<ULONG>(out.size()), 0);
    BCryptCloseAlgorithmProvider(alg, 0);
    return status == 0;
#else
    (void)password;
    (void)salt;
    (void)iterations;
    return false;
#endif
}

// ---- base64 ---------------------------------------------------------------

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 3 <= len; i += 3) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += kB64[(n >> 18) & 63];
        out += kB64[(n >> 12) & 63];
        out += kB64[(n >> 6) & 63];
        out += kB64[n & 63];
    }
    if (len - i == 1) {
        uint32_t n = data[i] << 16;
        out += kB64[(n >> 18) & 63];
        out += kB64[(n >> 12) & 63];
        out += "==";
    } else if (len - i == 2) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
        out += kB64[(n >> 18) & 63];
        out += kB64[(n >> 12) & 63];
        out += kB64[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

std::vector<uint8_t> base64_decode(const char* s, size_t len) {
    int8_t rev[256];
    std::memset(rev, -1, sizeof(rev));
    for (int i = 0; i < 64; ++i) {
        rev[static_cast<uint8_t>(kB64[i])] = static_cast<int8_t>(i);
    }

    std::vector<uint8_t> out;
    out.reserve(len / 4 * 3);
    uint32_t buf = 0;
    int bits = 0;
    for (size_t i = 0; i < len; ++i) {
        int8_t v = rev[static_cast<uint8_t>(s[i])];
        if (v < 0) {
            continue; // skip '=', whitespace, newlines
        }
        buf = (buf << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buf >> bits) & 0xff));
        }
    }
    return out;
}

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
    if (!platform_random(ptr, size)) {
        return JS_ThrowInternalError(ctx, "getRandomValues: RNG failure");
    }
    return JS_DupValue(ctx, argv[0]);
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

    std::vector<uint8_t> pw_bytes = pw != nullptr ? base64_decode(pw, pw_len) : std::vector<uint8_t>{};
    std::vector<uint8_t> salt_bytes = salt != nullptr ? base64_decode(salt, salt_len) : std::vector<uint8_t>{};
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
    if (!platform_pbkdf2_sha512(pw_bytes, salt_bytes, static_cast<uint32_t>(iterations), out)) {
        return JS_ThrowInternalError(ctx, "Pbkdf2.derive: PBKDF2 failure");
    }

    std::string b64 = base64_encode(out.data(), out.size());
    return JS_NewStringLen(ctx, b64.data(), b64.size());
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

    // Pbkdf2.derive
    JSValue pbkdf2 = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, pbkdf2, "derive", JS_NewCFunction(ctx, js_pbkdf2_derive, "derive", 5));
    JS_SetPropertyStr(ctx, global, "Pbkdf2", pbkdf2);

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
