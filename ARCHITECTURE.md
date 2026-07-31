# ton-walletkit-core — architecture sketch

A **cross-platform C++ core** that runs the canonical `@ton/walletkit` JS in an embedded
**QuickJS** engine and exposes a **JSON `send`/`receive` API** (+ updates). One core serves Windows, iOS,
Android, macOS, Linux; each platform provides a thin binding and a few host delegates. It unifies what
kit-ios (JSCore + Swift shims) and kit-android (WebView/QuickJS + Kotlin shims) each do per-platform into
a single portable host.

The ABI is **inspired by this repo's tdjson fork** (`Libraries/tdlib`, `td_json_client` + `ClientJson`) but
adapted to walletkit rather than copied. Kept from the fork: **correlation is a native
`unsigned long long request_id`** — passed to `send`, returned from `receive` as an out-param, never
injected as `@extra`/`@client_id` into the JSON (no per-call string splice on send, no mutex-guarded
`extra_` map + parse on receive) — and the `send`/`receive`-loop delivery model, which the C# app already
runs for `Telegram.Td`. Adapted for walletkit: an **opaque `twk_client*` handle** with explicit
create/destroy (each client owns a QuickJS runtime + thread, unlike tdlib's global `client_id` registry);
a request envelope of **`method` + free-form `params`** mirroring the bridge's own call shape (walletkit's
params are object / bare-string / positional-array, which tdlib's `@type`+fields can't express); and **no
`execute`** (walletkit calls are promise-based — everything is async `send`/`receive`).

> Status: **proposal / sketch.** Interfaces below are illustrative, not final.

## Goals / non-goals
- **Goal:** portable core, no per-platform JS host duplication; canonical JS (no fork, tracks upstream);
  a language-agnostic JSON ABI like TDLib; networking/secure-storage delegated to the host (so Windows
  can route through **TDLib `sendTonCenterApiRequest`** while other platforms use their own HTTP).
- **Non-goal:** Windows/C++-WinRT specifics in the core. No bundled HTTP stack (delegated). No reimpl of
  wallet crypto/protocol — that stays in the JS.

## Layout
```
ton-walletkit-core/
├── CMakeLists.txt
├── include/twk/
│   ├── twk.h              # C ABI: twk_client_create/destroy, twk_send(id,method,params), twk_receive(*id)
│   └── twk_delegates.h    # host delegates (http, sse, storage, random, log) + completion calls
├── src/
│   ├── client.cpp         # request/response/update queues, thread, dispatch
│   ├── engine/
│   │   ├── js_runtime.cpp # QuickJS runtime/context lifecycle; loads the embedded bundle
│   │   ├── event_loop.cpp # job-queue pump + timer heap + async completions
│   │   └── host_fns.cpp   # registers native globals into JS
│   ├── shims/             # the "just an engine" gap — ports of kit-ios JSCore/Polyfilling/*
│   │   ├── random.cpp     # crypto.getRandomValues -> RandomDelegate (CSPRNG)
│   │   ├── console.cpp    # console.* -> log delegate
│   │   ├── timers.cpp     # setTimeout/clearTimeout/setInterval -> event loop
│   │   ├── fetch.cpp      # THIN fetch (JSON GET/POST + AbortController) -> HttpDelegate
│   │   └── event_source.cpp # EventSource (SSE) -> SseDelegate
│   ├── bridge/transport.cpp # native<->JS envelopes (kind:call/response/event) + storage.*
│   └── generated/bundle.h # the JS bundle embedded as a byte array (build step)
├── js/                    # the JS QuickJS runs: ported bridge + kit-ios polyfills
│   ├── src/               # walletkit-core-bridge (transport bound to twk host fns)
│   └── build.mjs          # esbuild -> single bundle -> embedded into generated/bundle.h
└── bindings/
    ├── csharp/            # P/Invoke over twk.h + the typed facade we already wrote
    ├── swift/             # thin wrapper mimicking kit-ios TONWalletKit
    └── kotlin/            # thin wrapper mimicking kit-android
```

## The ABI (`twk.h`) — fork-inspired, walletkit-shaped
Fork ideas (native `request_id`, `send`/`receive` loop) on a walletkit-shaped surface (opaque handle,
`method`+`params` envelope, no `execute`):
```c
typedef struct twk_client twk_client;

twk_client* twk_client_create (const twk_delegates* delegates, void* user);   // owns a QuickJS runtime + thread
void        twk_client_destroy(twk_client*);                                  // deterministic teardown

// async request: caller picks request_id; params is free-form JSON (object | array | string | number)
void        twk_send   (twk_client*, unsigned long long request_id, const char* method, const char* params_json);
// next message for THIS client (handle-scoped, so no client_id out-param). NULL on timeout.
const char* twk_receive(twk_client*, double timeout, unsigned long long* request_id);
```
- Send:     `twk_send(c, 7, "createSignerFromMnemonic", "{\"mnemonic\":[…],\"mnemonicType\":\"ton\"}")` — `method` is first-class; `params_json` mirrors the bridge call shape. Bare-string calls pass `"\"ton://…\""`; approve/reject pass a positional array `"[event, response]"`.
- Response: `receive` returns `{"result":{"signerId":"…","publicKey":"…"}}` and sets `*request_id = 7`.
- Error:    `receive` returns `{"error":{"code":…,"message":"…"}}` with the same `*request_id` — binding rejects the pending task.
- Update:   `receive` returns `{"event":{…}}` and sets `*request_id = 0` (unsolicited TON Connect event; no correlated request).

Correlation is the native `request_id`; the binding owns its `id → TaskCompletionSource`/continuation map
(same idea as the fork's C# side), so the core never splices or parses `@extra`. The host runs one
`receive`-loop thread per client; string ownership follows TDLib — the returned buffer is valid until the
next `receive` on that thread.

## Delegates (`twk_delegates.h`) — the platform seam
Passed to `twk_client_create` (per-client, so storage can be per-account and `user` carries the client's
context); the core never does I/O itself. Async delegates hand back a `token` and complete via the
`twk_*_respond` calls, which take the `twk_client*` (callable from any thread; the core marshals to that
client's JS thread). `user` is the pointer given to `twk_client_create`.
```c
typedef struct {
  // HTTP — thin: JSON GET/POST only (that's all BaseApiClient needs). Windows -> TDLib sendTonCenterApiRequest.
  void (*http_request)(void* user, int64_t token, const char* method, const char* url,
                       const char* headers_json, const char* body /*nullable*/);
  // SSE — the TON Connect relay. deliver events via twk_sse_event / close via twk_sse_closed.
  void (*sse_open) (void* user, int64_t token, const char* url, const char* headers_json);
  void (*sse_close)(void* user, int64_t token);
  // Secure storage — mnemonics/keys/sessions. Windows -> PasswordVault, iOS -> Keychain, Android -> Keystore.
  void (*storage_get)   (void* user, int64_t token, const char* key);   // async -> twk_storage_respond
  void (*storage_set)   (void* user, const char* key, const char* value);
  void (*storage_remove)(void* user, const char* key);
  void (*storage_clear) (void* user);
  // Crypto RNG — sync, must be a CSPRNG (BCryptGenRandom / SecRandom / getrandom).
  void (*random_bytes)(void* user, uint8_t* out, size_t len);
  void (*log)(void* user, int level, const char* message);
} twk_delegates;

// host -> core completions (identify the client + the pending token):
void twk_http_respond   (twk_client*, int64_t token, int status, const char* headers_json, const char* body);
void twk_sse_event      (twk_client*, int64_t token, const char* data);
void twk_sse_closed     (twk_client*, int64_t token, const char* error /*NULL = normal*/);
void twk_storage_respond(twk_client*, int64_t token, const char* value /*NULL = missing*/);
```
Note the two id spaces are distinct: **`request_id`** correlates an app request↔response across the ABI
top edge; **`token`** correlates a core-initiated delegate call↔its completion across the bottom edge.
After `twk_client_destroy` the host must not fire `twk_*_respond` for that handle (the core drops
in-flight tokens on teardown and ignores late completions).

## Threading & event loop
- One QuickJS runtime+context per client, pinned to a **dedicated worker thread** (JS is single-threaded).
- **Input queue** (thread-safe): requests from `send` (carrying `request_id`), plus async completions
  (`twk_http_respond`, sse events, `twk_storage_respond`, timer fires). **Output queue**: `(request_id,
  json)` pairs — responses (`request_id` echoed from the originating `send`) and updates (`request_id = 0`)
  — drained by `receive`, which unpacks `request_id` into its out-param. Queues are per-client (the handle
  scopes them), so there's no cross-client `client_id` to carry.
- Loop: pop input → dispatch into JS → `JS_ExecutePendingJob` until the microtask queue drains (promises)
  → recompute next timer deadline → wait on the input condvar with that timeout. Standard actor loop.
- Delegate calls go *out* on the JS thread; the host does the work off-thread and calls back a
  `twk_*_respond`, which enqueues onto the input queue. No blocking the JS thread on I/O.

## Shims (the "just an engine" gap) — ports of kit-ios `JSCore/Polyfilling/*`
| global | impl | size | maps to |
|---|---|---|---|
| `crypto.getRandomValues` | `shims/random` | tiny | `random_bytes` delegate |
| `console.*` | `shims/console` | trivial | `log` delegate |
| `setTimeout/clearTimeout/setInterval` | `shims/timers` | moderate | event-loop timer heap |
| `fetch` (JSON GET/POST + `AbortController`) | `shims/fetch` | **small** | `http_request` delegate |
| `EventSource` | `shims/event_source` | moderate | `sse_open`/`twk_sse_event` |

Pure-JS polyfills that just **bundle** (reused from `walletkit-ios-bridge/src/polyfills`): `Buffer`,
`URL`, `TextEncoder/Decoder`, `EventTarget/Event/CustomEvent/MessageEvent`, `randomUUID`.
**Deferred:** full fetch (Blob/FormData), `WebSocket` (streaming only), native PBKDF2 (perf; JS works).

Rationale for the thin fetch: `BaseApiClient` only uses `getJson`/`postJson` (JSON, `AbortController`
timeout) — no Blob/FormData/streaming/redirects — so `fetch` is a small JSON shim over `http_request`,
and walletkit's own `ApiClientToncenter` keeps doing the endpoint building + parsing in JS (no native
reimpl; stays canonical). Windows backs `http_request` with `sendTonCenterApiRequest`.

### Mapping `http_request` → TDLib (the Windows binding)
The TDLib schema (`td_api.tl`) is:
```
tonCenterApiRequestTypeGet  query:string   = TonCenterApiRequestType;
tonCenterApiRequestTypePost payload:string = TonCenterApiRequestType;
sendTonCenterApiRequest endpoint:string type:TonCenterApiRequestType = Text;
```
So TDLib wants **endpoint path + (query | payload)**, not a full URL — and it supplies the base URL and
API key itself. The delegate therefore passes the **full URL** and lets the binding split it, which keeps
the core host-agnostic (the libcurl reference host wants the whole URL):
```csharp
var uri = new Uri(url);                              // walletkit built this
var type = method == "GET"
    ? (TonCenterApiRequestType)new TonCenterApiRequestTypeGet(uri.Query.TrimStart('?'))
    : new TonCenterApiRequestTypePost(body);
var text = await _clientService.SendAsync(new SendTonCenterApiRequest(uri.AbsolutePath, type));
twk_http_respond(client, token, 200, null, ((Text)text).TextValue);
```
Because TDLib owns the base URL, walletkit is configured with a placeholder base on Windows and only the
path/query survive the mapping. Tests never use TDLib — the desktop reference host (libcurl) backs the
same delegate, so the core and the JS above it are identical in both worlds.

## The JS layer
`js/` builds a **walletkit-core-bridge** = the ported bridge (kind:call/response/event envelopes) with
its transport bound to two host globals instead of a WebView channel:
- native → JS: the host calls `walletkitBridge.handleNativeCall(method, params, request_id)` synchronously
  (`method`/`params` are exactly the two `twk_send` args; the bridge re-forms its own `{kind:call,id,…}`).
- JS → native: `globalThis.__twk_emit(json, request_id)` → output queue, where `json` is a
  `{result}` / `{error}` / `{event}` envelope. Responses echo the call's `request_id`; unsolicited
  walletkit events emit with `request_id = 0`.
Plus the shim globals above. Storage uses a host global (`__twk_storage_*`) → `storage_*` delegate.
The bundle is embedded into `generated/bundle.h` at build (esbuild → byte array), so the core has no
filesystem dependency.

## Data flows
(`c` = the `twk_client*` handle.)
- **init:** `twk_send(c, 1, "init", "{config}")` → bridge builds the kit (network via `fetch`→delegate,
  storage via host fns) → `__twk_emit({result:{}}, 1)` → `receive` yields it with `*request_id = 1`.
- **getBalance:** `twk_send(c, 2, "getBalance", "{\"walletId\":…}")` → `wallet.getBalance()` →
  `ApiClientToncenter` → `fetch(toncenter)` → `http_request(user, token, …)` → host: `sendTonCenterApiRequest`
  → `twk_http_respond(c, token, …)` → Promise resolves → parse → `__twk_emit({result:{value:…}}, 2)`.
- **TON Connect:** `twk_send(c, 3, "handleTonConnectUrl", "\"ton://…\"")` → relay via `EventSource` →
  `sse_open(user, token, …)` → host SSE → `twk_sse_event(c, token, …)` → walletkit →
  `__twk_emit({event:{type:"connectRequest",…}}, 0)` (unsolicited; `receive` reports `*request_id = 0`).
  Host approves with a fresh `twk_send(c, 4, "approveConnectRequest", "[event, response]")`.

## Build
- **CMake**; QuickJS-ng vendored (portable C, MIT). Bundle embedded via a custom command
  (`js/build.mjs` → `generated/bundle.h`). No external runtime deps (JSON stays as strings; the JS parses).
- Targets: Windows MSVC (arm64/x64), iOS `xcframework`, Android NDK (`.so` per ABI), macOS, Linux.
- `bindings/csharp` P/Invokes `twk.h` and owns the `request_id → TaskCompletionSource` map + the
  `receive` loop (mirroring the fork's `Telegram.Td` client); the typed facade we already wrote is
  retargeted from `WalletKitEngine` (WebView2) → `twk_send`/`twk_receive`. Swift/Kotlin bindings do the
  same over the same C ABI, mirroring kit-ios/kit-android.

## What carries over from the current Windows work
- **Reused:** the ported bridge JS + kit-ios polyfills (→ `js/`); the generated OpenAPI models (→ binding
  DTOs); the C# `TonWalletKit`/`TonWallet` facade (→ `bindings/csharp` over `twk.h`).
- **Replaced:** `WalletKitEngine` (WebView2) → the C++ QuickJS host. `BridgedJSApiClient` (native
  toncenter reimpl) is **dropped** in favor of walletkit's own JS client + the thin fetch delegate.

## Open questions / risks
- QuickJS-**ng** (maintained; more web APIs) vs Bellard quickjs → recommend ng.
- Interpreter perf for PBKDF2 (mnemonic import, 2048 iters) → maybe a native `pbkdf2` host fn later.
- BigInt is native in QuickJS (amounts OK). GC/heap tuning per client.
- Delegate reentrancy/threading contract; cancellation (AbortController → `sse_close`/http cancel token).
- Bundle size / cold-start eval time of the walletkit bundle in the interpreter.
