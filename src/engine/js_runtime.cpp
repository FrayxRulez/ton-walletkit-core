//
// ton-walletkit-core — QuickJS runtime/context wrapper (internal).
//
#include "engine/js_runtime.h"

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

JsRuntime::JsRuntime() {
    rt_ = JS_NewRuntime();
    ctx_ = JS_NewContext(rt_);
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
