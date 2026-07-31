# ton-walletkit-core — implementation roadmap

Companion to `ARCHITECTURE.md`. Ordered as **vertical, demoable milestones** (M0→M8), each proving one
riskier thing than the last and each with an **exit criterion** + its own tests, so the project is always
in a runnable state. Cross-cutting tooling (the test harness, mock servers, CI, sanitizers) is called out
once in **§ Standing harness** and then *used* by every milestone rather than bolted on at the end.

Legend: 🎯 exit criterion · 🧪 tests introduced · ⚠️ primary risk being de-risked.

---

## M0 — Walking skeleton (de-risk the engine)
⚠️ Prove QuickJS eval + host globals + `send`/`receive` correlation + the worker-thread event loop, with a
**stub JS** (no walletkit yet).
- [ ] Scaffold repo: `CMakeLists.txt`, layout from ARCHITECTURE, `.clang-format`, `.gitignore`.
- [ ] Vendor **QuickJS-ng** as a submodule; build it via CMake; confirm it links on the dev OS.
- [ ] `engine/js_runtime`: create runtime+context, eval a source buffer, register global native fns.
- [ ] `engine/event_loop`: input/output queues, `JS_ExecutePendingJob` pump, timer heap, one worker thread.
- [ ] `client.cpp` + `twk.h`: `twk_client_create/destroy`, `twk_send(id,method,params)`, `twk_receive(*id)`.
- [ ] `bridge/transport`: native→JS `handleNativeCall`, JS→native `__twk_emit`; `{result|error|event}` envelope.
- [ ] Stub bundle: a few lines of JS that register the globals and echo `method`/`params` back as `result`.
- 🎯 `twk-cli` (see §Standing) sends `{"method":"ping","params":{"x":1}}` and reads back the echo with the
  matching `request_id`; `create`→`destroy`→`create` churn is clean under a leak checker.
- 🧪 C++ unit tests: correlation (`request_id` echo), `request_id=0` for an unsolicited emit, timeout returns
  NULL, timers fire in order, teardown drops pending items.

## M1 — Real walletkit bundle, pure-JS call
⚠️ Prove the **canonical walletkit JS actually loads and runs** in QuickJS-ng (BigInt, module shape, polyfills).
- [ ] `js/`: adapt the ported bridge transport to the two host globals (drop the WebView2 channel); reuse the
      **kit-ios polyfills** (`Buffer`/`URL`/`TextEncoder`/`Event*`/`randomUUID`).
- [ ] `js/build.mjs`: esbuild → single bundle; CMake custom command embeds it → `generated/bundle.h` (byte array).
- [ ] Shims needed by pure calls: `shims/random` (RNG delegate), `shims/console` (log), `shims/timers`.
- [ ] Standalone JS smoke: load the bundle in Node with mocked host globals; assert it evaluates + registers.
- 🎯 `twk-cli` runs `createTonMnemonic` → 24 words returned through `send`/`receive`.
- 🧪 JS-level test (Node) that the bundle boots; C++ test that a pure walletkit call round-trips.

## Perf pass (done, post-M1) — measured, not guessed
`tools/twk-bench` splits startup from per-call cost; `walletKit.benchCrypto` times each
primitive. Baseline → now (MSVC x64):

| | before | after |
|---|---|---|
| `createMnemonic` | 16.7 s mean | **0.4 s** |
| startup (bundle load) | 1380 ms | **1100 ms** |
| `hmac_sha512` / `sha512` / `sha256` | 48.9 / 14.9 / 5.0 ms | **0.02 / 0.04 / 0.04 ms** |
| echo (warm round trip) | 0.1 ms | 0.1 ms |

- **Native SHA/HMAC/PBKDF2** (`src/util/crypto`, BCrypt, cached providers) replacing jssha,
  which is punishing in the interpreter. `js/src/polyfills/crypto-primitives.ts` shims
  `@ton/crypto-primitives`, falling back to pure JS when the host lacks `__twk_crypto`.
  `twk_crypto_kat` proves bit-identical output against RFC/OpenSSL vectors.
- **Bytecode precompile** (`tools/twk-bundlec`) removes the per-client parse.

**Still open:** the remaining ~1.1 s startup is *executing* the bundle (walletkit's object
graph), not parsing — only reducible by importing less of walletkit. Revisit after M3, and
note `createMnemonic` is inherently variable (geometric, p=1/256 per attempt).

## M2 — Networking via the HTTP delegate + reference host
⚠️ Prove the **delegate seam** and the async `fetch`→`http_request`→`twk_http_respond`→Promise flow end to end.
- [ ] `shims/fetch`: thin JSON GET/POST + `AbortController` → `http_request` delegate.
- [ ] `twk_delegates.h` + completion calls (`twk_http_respond`, …); wire `user`/token plumbing + teardown guard.
- [ ] **Reference host** (desktop, test-only): `http_request` backed by libcurl; RNG from OS CSPRNG; stub SSE/storage.
- 🎯 `twk-cli` inits the kit and runs `getBalance` for a known **testnet** address; balance comes back.
- 🧪 Deterministic CI variant against the **mock toncenter server** (recorded fixtures) — no live network in CI;
  timeout + HTTP-error + malformed-body paths exercised.

## M3 — Storage + full wallet lifecycle
⚠️ Prove the stateful **signer → adapter → wallet** object-by-reference flow and secret persistence.
- [ ] Storage host globals (`__twk_storage_*`) → `storage_*` delegate; reference in-memory + file-backed store.
- [ ] Verify inferred payload shapes against real responses (network param, `mnemonicType`, transfer-request
      fields, approve/reject array) — reconcile the C# facade's guesses.
- 🎯 E2E happy path: `mnemonic → createSignerFromMnemonic → createV5R1WalletAdapter → addWallet → getBalance
      → createTransferTonTransaction (fake signature) → getSignedSendTransaction`; a fresh client restores
      the wallet **by rebuilding it from a persisted mnemonic**.
- 🧪 Lifecycle integration test (mock toncenter); storage delegate contract test (get-missing, set/get/remove/clear).

> **Correction (found while implementing):** the original criterion said "wallet survives a destroy/recreate
> via storage". That is impossible — **walletkit persists neither wallets nor signers** (`WalletManager` holds
> an in-memory Map; its `storageKey`/`loadWallets` are commented out in the shipped code). The storage
> delegate holds TON Connect sessions, the bridge `lastEventId`, and the event store. So the host must
> persist the mnemonic/secret itself, deliberately and in a secure store, and rebuild
> signer → adapter → wallet on startup. Storage must still be confidential, because session private keys
> live there. See `docs/API.md`.

**M3 done.** Verified payload shapes are recorded in `docs/API.md` (several earlier assumptions were wrong —
`walletId` is a base64 hash, `recipientAddress` must be user-friendly form, transfer fields are
`recipientAddress`/`transferAmount`). Public JSON API names mirror kit-ios, with object arguments replaced by
ids because a JSON ABI cannot carry objects.

## M4 — TON Connect (SSE)
⚠️ Prove **unsolicited updates** (`request_id=0`) and the SSE relay path.
- [ ] `shims/event_source`: `EventSource` → `sse_open`/`twk_sse_event`/`twk_sse_closed`; reference SSE client (libcurl).
- 🎯 `handleTonConnectUrl` → `receive` yields `{"event":{type:"connectRequest",…}}` → `approveConnectRequest`
      round-trips (against a test dapp or a mocked relay).
- 🧪 SSE reconnect/close/error handling; event ordering vs interleaved responses.

## M5 — Hardening & full test matrix
⚠️ Correctness under stress, memory safety, thread safety.
- [ ] Sanitizer builds: ASan/LSan/UBSan and (separately) TSan; run the whole suite under each.
- [ ] Robustness: malformed/oversized JSON in, delegate errors/late completions, cancellation, rapid
      create/destroy churn, concurrent `send` from many threads while one `receive` loops.
- [ ] **Contract/golden tests**: capture request/response fixtures and diff shapes against **kit-android** tests
      so we stay wire-compatible with the canonical libs.
- [ ] QuickJS resource limits: memory cap, stack cap, per-call interrupt/time budget.
- 🎯 CI green on Linux + macOS + Windows **desktop**; sanitizers clean; contract tests pass.
- 🧪 The above become permanent CI gates.

## M6 — Windows / UWP binding (the product integration)
⚠️ Ship it inside Unigram; retire the WebView2 engine.
- [ ] `bindings/csharp`: P/Invoke over `twk.h`; the `receive` loop on a dedicated thread; `request_id →
      TaskCompletionSource` map (reuse the `Telegram.Td` client pattern).
- [ ] C# delegates: `http_request` → **TDLib `sendTonCenterApiRequest`**; SSE; `storage_*` → PasswordVault;
      RNG → `BCryptGenRandom`.
- [ ] Retarget the existing `TonWalletKit`/`TonWallet` facade from `WalletKitEngine` (WebView2) → `twk_send`/
      `twk_receive`; keep the generated OpenAPI models as DTOs.
- [ ] Native packaging: build `twk.dll` per arch (x64/arm64), include in `Telegram.csproj`; verify under the
      **.NET Native (UWP AOT)** toolchain; marshalling + `[UnmanagedCallersOnly]`/pinned-delegate story for completions.
- 🎯 In-app smoke: create wallet, show balance, send a testnet tx, complete a TON Connect approval — all via
      the native core; the WebView2 `WalletKitEngine` + `BridgedJSApiClient` are removed.
- 🧪 Managed integration tests (delegates mocked); an in-app manual test checklist.

## M7 — Cross-platform bindings (iOS / Android) — later / optional
- [ ] `bindings/swift`: thin wrapper mimicking kit-ios `TONWalletKit`; delegates via URLSession / Keychain / SecRandom; **xcframework** packaging.
- [ ] `bindings/kotlin`: thin wrapper mimicking kit-android; delegates via OkHttp / Keystore; **.so per ABI** + AAR.
- 🎯 The kit-ios / kit-android facade surface reproduced over the same C ABI; parity tests pass on each.

## M8 — Release engineering, upstream sync, security review, docs
- [ ] **CMake cross-compile matrix** + presets (Win arm64/x64, iOS, Android NDK per ABI, macOS, Linux); reproducible bundle build.
- [ ] CI produces versioned artifacts for every target; release tagging.
- [ ] **Upstream-sync process**: bump `@ton/walletkit`, rebuild bundle, re-run contract tests, diff shapes — documented runbook.
- [ ] **Security review**: mnemonic/key handling (never logged, zeroize where possible), storage-at-rest, delegate
      trust boundaries, QuickJS sandbox limits, **npm supply-chain audit** of the bundle; short threat model.
- [ ] Docs: `README`, API reference, per-platform integration guide, migration notes.
- 🎯 Tagged release with artifacts + docs; sync runbook exercised once against a real walletkit bump.

---

## Standing harness (built in M0, used throughout)
- **`twk-cli`** — a tiny desktop executable linking the core + the reference host: reads JSON lines on stdin,
  `twk_send`s them, prints `receive` output. The primary manual + scripted E2E driver (our `tdcli` analog).
- **Reference host** — desktop delegate impls (libcurl HTTP, libcurl SSE, in-memory/file storage, OS RNG) so
  the core is fully exercisable with **zero** WebView2/TDLib/platform dependency.
- **Mock toncenter server** — replays recorded fixtures for deterministic, offline CI; a recorder captures real
  testnet responses to refresh fixtures.
- **Unit tests** — Catch2 (or gtest): event loop, timers, correlation, envelope, teardown, delegate contracts.
- **Integration tests** — scripted `twk-cli` sessions (or a C++ harness) driving the milestone E2E flows against
  the mock server; golden-file diffing of request/response shapes.
- **CI matrix** — Linux/macOS/Windows desktop from M0; sanitizer jobs from M5; cross targets from M8.

## Standing / cross-cutting concerns
- **Secret hygiene** from day one: mnemonics/keys never cross `log`, never land in fixtures; scrub test output.
- **ABI stability**: treat `twk.h` as a contract; additive changes only once M6 ships.
- **Deferred (explicitly out of initial scope):** full `fetch` (Blob/FormData), `WebSocket` streaming, native
  PBKDF2 acceleration, advanced ops (swap/staking/gasless/streaming) — revisit after M6.

## Critical path (shortest route to a working in-app wallet)
`M0 → M1 → M2 → M3 → M6`. M4 (TON Connect) and M5 (hardening) are required for **release** but not for the
first internal demo; M7/M8 follow. The riskiest unknowns (QuickJS running walletkit; the delegate/fetch
seam) are settled by the end of **M2**.
