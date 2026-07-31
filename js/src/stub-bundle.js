// M0/M1 stub bundle (kit-ios shape): defines globalThis.walletKit and signals
// readiness, so the native-await transport can be exercised without the full
// @ton/walletkit. Replaced by the real esbuild bundle in the integrate task.
(function () {
  "use strict";

  globalThis.walletKit = {
    // Echo the (single) argument back — used to prove the native<->JS round trip
    // and request_id correlation. Async so it exercises the promise-await path.
    async echo(value) {
      return value ?? null;
    },
    // Always rejects — exercises the {error} envelope.
    async fail() {
      throw new Error("boom");
    },
  };

  if (typeof globalThis.__twk_ready === "function") {
    globalThis.__twk_ready();
  }
})();
