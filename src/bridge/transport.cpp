//
// ton-walletkit-core — native <-> JS bridge transport (internal).
//
#include "bridge/transport.h"

#include "bundle.h" // generated: twk_bundle_js / twk_bundle_js_len
#include "client.h"
#include "engine/js_runtime.h"

namespace twk {
namespace bridge {

namespace {

// JS -> native: __twk_emit(envelope, requestId)
//   envelope  : a {result} | {error} | {event} object
//   requestId : the correlation id as a BigInt (0 for unsolicited updates)
JSValue twk_emit(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    auto* client = static_cast<Client*>(JS_GetContextOpaque(ctx));
    if (client == nullptr || argc < 1) {
        return JS_UNDEFINED;
    }

    std::string json = client->js()->toJson(argv[0]);

    uint64_t request_id = 0;
    if (argc >= 2) {
        JS_ToBigUint64(ctx, &request_id, argv[1]);
    }

    client->emit(request_id, std::move(json));
    return JS_UNDEFINED;
}

} // namespace

bool install(JsRuntime& js, std::string* error) {
    js.registerGlobal("__twk_emit", twk_emit, 2);

    std::string code(reinterpret_cast<const char*>(twk_bundle_js), twk_bundle_js_len);
    return js.eval(code, "<bundle>", nullptr, error);
}

void dispatch(JsRuntime& js, Client& client, uint64_t request_id, const std::string& method,
              const std::string& params_json) {
    JSContext* ctx = js.context();

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue bridge = JS_GetPropertyStr(ctx, global, "walletkitBridge");
    JSValue handle = JS_GetPropertyStr(ctx, bridge, "handleNativeCall");

    JSValue js_method = JS_NewString(ctx, method.c_str());
    JSValue js_params = JS_NULL;
    if (!params_json.empty()) {
        js_params = js.parseJson(params_json);
        if (JS_IsException(js_params)) {
            JS_FreeValue(ctx, JS_GetException(ctx)); // clear + fall back to null
            js_params = JS_NULL;
        }
    }
    JSValue js_rid = JS_NewBigUint64(ctx, request_id);

    JSValue argv[3] = {js_method, js_params, js_rid};
    JSValue ret = JS_Call(ctx, handle, bridge, 3, argv);
    if (JS_IsException(ret)) {
        (void)js.takeExceptionText();
        client.emit(request_id, "{\"error\":{\"message\":\"bridge dispatch failed\"}}");
    }

    JS_FreeValue(ctx, ret);
    JS_FreeValue(ctx, js_rid);
    JS_FreeValue(ctx, js_params);
    JS_FreeValue(ctx, js_method);
    JS_FreeValue(ctx, handle);
    JS_FreeValue(ctx, bridge);
    JS_FreeValue(ctx, global);
}

} // namespace bridge
} // namespace twk
