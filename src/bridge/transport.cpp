//
// ton-walletkit-core — native <-> JS bridge transport (internal).
//
// kit-ios-shaped: the native core calls globalThis.walletKit[method](...args)
// directly and awaits the returned promise with native then/catch continuations
// that emit {result} / {error} for the request_id. The JS side is just the
// canonical walletKit object (+ polyfills) — no routing or registry code in JS.
//
#include "bridge/transport.h"

#include <vector>

#include "bundle.h" // generated: twk_bundle_js / twk_bundle_js_len
#include "client.h"
#include "engine/js_runtime.h"
#include "host_context.h"

namespace twk {
namespace bridge {

namespace {

Client* client_of(JSContext* ctx) {
    auto* hc = static_cast<HostContext*>(JS_GetContextOpaque(ctx));
    return hc != nullptr ? hc->client : nullptr;
}

// __twk_ready() — the bundle signals that walletKit is constructed and ready.
JSValue js_ready(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/, JSValueConst* /*argv*/) {
    if (Client* client = client_of(ctx)) {
        client->onJsReady();
    }
    return JS_UNDEFINED;
}

uint64_t rid_from_data(JSContext* ctx, JSValueConst* func_data) {
    uint64_t rid = 0;
    JS_ToBigUint64(ctx, &rid, func_data[0]);
    return rid;
}

// Emit a single-key envelope ({<key>: value}) for the given request id.
void emit_envelope(JSContext* ctx, Client& client, uint64_t request_id, const char* key, JSValueConst value) {
    JSValue env = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, env, key, JS_DupValue(ctx, value));
    std::string json = client.js()->toJson(env);
    JS_FreeValue(ctx, env);
    client.emit(request_id, std::move(json));
}

// promise.then fulfilment -> {result: value}
JSValue on_resolve(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv, int /*magic*/,
                   JSValueConst* func_data) {
    Client* client = client_of(ctx);
    if (client != nullptr) {
        emit_envelope(ctx, *client, rid_from_data(ctx, func_data), "result", argc > 0 ? argv[0] : JS_UNDEFINED);
    }
    return JS_UNDEFINED;
}

// promise.then rejection -> {error: {message}}
JSValue on_reject(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv, int /*magic*/,
                  JSValueConst* func_data) {
    Client* client = client_of(ctx);
    if (client == nullptr) {
        return JS_UNDEFINED;
    }
    JSValue error = JS_NewObject(ctx);
    const char* msg = argc > 0 ? JS_ToCString(ctx, argv[0]) : nullptr;
    JS_SetPropertyStr(ctx, error, "message", JS_NewString(ctx, msg != nullptr ? msg : "rejected"));
    if (msg != nullptr) {
        JS_FreeCString(ctx, msg);
    }
    emit_envelope(ctx, *client, rid_from_data(ctx, func_data), "error", error);
    JS_FreeValue(ctx, error);
    return JS_UNDEFINED;
}

// Attach native continuations to a returned value: if thenable, wire then/catch;
// otherwise it's a synchronous result -> emit it directly.
void attach(JsRuntime& js, Client& client, uint64_t request_id, JSValueConst result) {
    JSContext* ctx = js.context();
    JSValue then = JS_IsObject(result) ? JS_GetPropertyStr(ctx, result, "then") : JS_UNDEFINED;

    if (JS_IsFunction(ctx, then)) {
        JSValue data[1] = {JS_NewBigUint64(ctx, request_id)};
        JSValue on_f = JS_NewCFunctionData(ctx, on_resolve, 1, 0, 1, data);
        JSValue on_r = JS_NewCFunctionData(ctx, on_reject, 1, 0, 1, data);
        JS_FreeValue(ctx, data[0]);

        JSValue then_args[2] = {on_f, on_r};
        JSValue ret = JS_Call(ctx, then, result, 2, then_args);
        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, on_f);
        JS_FreeValue(ctx, on_r);
    } else {
        emit_envelope(ctx, client, request_id, "result", result);
    }

    JS_FreeValue(ctx, then);
}

} // namespace

bool install(JsRuntime& js, std::string* error) {
    js.registerGlobal("__twk_ready", js_ready, 0);

    std::string code(reinterpret_cast<const char*>(twk_bundle_js), twk_bundle_js_len);
    return js.eval(code, "<bundle>", nullptr, error);
}

void dispatch(JsRuntime& js, Client& client, uint64_t request_id, const std::string& method,
              const std::string& params_json) {
    JSContext* ctx = js.context();

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue wallet_kit = JS_GetPropertyStr(ctx, global, "walletKit");
    JS_FreeValue(ctx, global);

    if (!JS_IsObject(wallet_kit)) {
        JS_FreeValue(ctx, wallet_kit);
        client.emit(request_id, "{\"error\":{\"message\":\"walletKit not ready\"}}");
        return;
    }

    JSValue fn = JS_GetPropertyStr(ctx, wallet_kit, method.c_str());
    if (!JS_IsFunction(ctx, fn)) {
        JS_FreeValue(ctx, fn);
        JS_FreeValue(ctx, wallet_kit);
        client.emit(request_id, "{\"error\":{\"message\":\"unknown method: " + method + "\"}}");
        return;
    }

    // params_json is a positional argument list: a JSON array is spread; null/empty
    // means no args; any other value is passed as a single argument.
    std::vector<JSValue> args;
    if (!params_json.empty()) {
        JSValue parsed = js.parseJson(params_json);
        if (JS_IsException(parsed)) {
            JS_FreeValue(ctx, JS_GetException(ctx));
            parsed = JS_NULL;
        }
        if (JS_IsArray(parsed)) {
            int64_t len = 0;
            JSValue len_val = JS_GetPropertyStr(ctx, parsed, "length");
            JS_ToInt64(ctx, &len, len_val);
            JS_FreeValue(ctx, len_val);
            for (int64_t i = 0; i < len; ++i) {
                args.push_back(JS_GetPropertyUint32(ctx, parsed, static_cast<uint32_t>(i)));
            }
            JS_FreeValue(ctx, parsed);
        } else if (JS_IsNull(parsed) || JS_IsUndefined(parsed)) {
            JS_FreeValue(ctx, parsed);
        } else {
            args.push_back(parsed); // takes ownership
        }
    }

    JSValue result = JS_Call(ctx, fn, wallet_kit, static_cast<int>(args.size()), args.data());
    for (JSValue arg : args) {
        JS_FreeValue(ctx, arg);
    }
    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, wallet_kit);

    if (JS_IsException(result)) {
        std::string text = js.takeExceptionText();
        JS_FreeValue(ctx, result);
        client.emit(request_id, "{\"error\":{\"message\":\"call threw\"}}");
        return;
    }

    attach(js, client, request_id, result);
    JS_FreeValue(ctx, result);
}

} // namespace bridge
} // namespace twk
