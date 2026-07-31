// M0 stub bridge bundle.
//
// Registers walletkitBridge.handleNativeCall and echoes each call straight back
// through the native transport (__twk_emit) as a {result} envelope, preserving
// the request_id correlation. This exists only to prove the native<->JS round
// trip in the walking skeleton; M1 replaces it with the real @ton/walletkit
// bundle produced by esbuild.
(function () {
  "use strict";

  globalThis.walletkitBridge = {
    handleNativeCall: function (method, params, requestId) {
      __twk_emit({ result: { method: method, params: params ?? null } }, requestId);
    },
  };
})();
