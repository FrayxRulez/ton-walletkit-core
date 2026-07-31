//
// ton-walletkit-core — native <-> JS bridge transport (internal).
//
#pragma once

#include <cstdint>
#include <string>

namespace twk {

class JsRuntime;
class Client;

namespace bridge {

// Register the host globals (__twk_emit) and evaluate the embedded bundle, which
// defines globalThis.walletkitBridge. Returns false + *error on eval failure.
bool install(JsRuntime& js, std::string* error);

// native -> JS: call walletkitBridge.handleNativeCall(method, params, requestId).
// `params_json` is parsed to a JS value (null when empty). On a synchronous throw,
// an {error} envelope is emitted for `request_id`. Responses/events flow back
// asynchronously through __twk_emit.
void dispatch(JsRuntime& js, Client& client, uint64_t request_id, const std::string& method,
              const std::string& params_json);

} // namespace bridge
} // namespace twk
