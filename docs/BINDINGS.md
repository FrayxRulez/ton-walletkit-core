# Writing a binding

A binding is three layers. Only the middle one is interesting to write; the other
two are generated or mechanical.

| layer | where it comes from | size (C#) |
|---|---|---|
| **DTOs** — walletkit's models | generated from walletkit's TypeScript by `scripts/generate-api` | ~55,000 lines |
| **Facade** — the kit-ios object model | generated from `api/facade.json` by `scripts/generate-facade.mjs` | ~670 lines |
| **Transport** — P/Invoke, JNI, Swift C interop… | hand-written, per platform | ~770 lines |

So porting to a new language is: add a DTO generator target, add a facade emitter,
and write the transport. The transport is the only part that needs judgement, and
this document is mostly about getting it right.

## 1. DTOs

`scripts/generate-api` already downloads openapi-generator and feeds it a spec
built from walletkit's own types. Adding a language is a generator flag:

| language | target | notes |
|---|---|---|
| C# | `csharp --library generichost` | hand-written converters, which .NET Native needs |
| Java | `java --library native` | or `apache-httpclient`; only the models are kept |
| Kotlin | `kotlin` | what kit-android generates |
| Swift | `swift5` / `swift6` | `Codable` models |
| C++/Qt | `cpp-qt-client` | `QString`/`QJsonObject` models — see §6 |

Keep only the model files and whatever tiny support they need; the rest of every
generator's output is an HTTP client we do not use, because our transport is the
C ABI. See `scripts/generate-api/run-openapi-generator.mjs` for how C# does it.

## 2. Facade

`api/facade.json` describes every object, method, argument and return type of the
kit-ios-shaped API, plus the TON Connect request objects and the event union. An
emitter is one module under `scripts/facade/` that turns that into source; the C#
one is ~450 lines and is the reference.

An emitter must handle:

- **Naming.** The schema is neutral camelCase (`transferTonTransaction`). C# emits
  `TransferTonTransactionAsync`, Swift would emit `transferTONTransaction`, Java
  `transferTonTransaction`. Overloads share a `name` and differ by `id`.
- **`custom` methods.** Five methods are declared in the schema but implemented by
  hand (`initialize`, `createWallet`, `send`, `addressBalance`, `connect`). Emit
  the declaration, skip the body, and let the language's mechanism for splitting a
  type across files carry the rest — C# uses `partial`; a language without that can
  emit an abstract base and hand-write the concrete class.
- **The special types**, which mean the same thing everywhere:

  | type | meaning |
  |---|---|
  | `amount` | an exact integer amount; the wire form is a decimal string. **Never parse it as a float** — that is a money bug |
  | `mnemonic` | the word list, wrapped so it cannot be mistaken for prose |
  | `bytes` | raw bytes; the ABI carries them as a **JSON array of numbers**, not base64 |
  | `chainId` | optional network id, defaulting to the first configured network |
  | `params:V5R1` | wallet parameters, which serialize themselves |
  | `fakeSignature` | an optional bool that becomes a signing-options object |
  | `object:Wallet` | another facade object, passed as the id the core knows it by |

## 3. Transport — the part that needs care

The C ABI is four functions (`include/twk/twk.h`) plus a delegate block
(`twk_delegates.h`). Everything below has bitten this project at least once.

**One receive thread per client.** `twk_receive` must not be called concurrently
for the same client. Run exactly one thread; hand results to whatever the language
uses for async completion.

**Do not complete on the receive thread.** A caller awaiting a result would
otherwise run its continuation inside the pump and stall every other request. C#
uses `TaskCreationOptions.RunContinuationsAsynchronously`; do the equivalent.

**Correlate by `request_id`, not by order.** Ids start at 1; **0 means an
unsolicited update** (a TON Connect request, a disconnect), which answers no
request and must go to the event path.

**The receive buffer dies on the next call.** Copy it before doing anything else.

**Teardown order.** Stop the pump, *join the receive thread*, then
`twk_client_destroy`, then release the delegate block. The delegates must outlive
the client: a late host completion arriving after destroy is normal, and the core
handles it — but only if the memory it points at is still there.

**Strings are UTF-8.** Not the platform's ANSI encoding, not UTF-16. Marshal
explicitly; several languages get this wrong by default.

**Envelopes.** Every reply is `{"result":…}` or `{"error":{"message":…}}`. Turn
the error half into whatever your language throws, at the transport layer, so the
facade never has to look.

**Delegate block layout.** `twk_delegates` is a plain struct of function pointers
in a fixed order (see `twk_delegates.h`). If your language builds it by hand, the
order is load-bearing and a mistake shows up as a crash in unrelated code. Keep
the callbacks rooted for as long as the client lives — a GC that moves or collects
them is a use-after-free.

**Storage is confidential.** It holds TON Connect session private keys. Encrypt it
at rest with whatever the platform offers.

## 4. What the host must provide

| delegate | what it is | note |
|---|---|---|
| `http_request` / `http_cancel` | one-shot HTTP | Unigram routes it through TDLib; the reference host uses WinHTTP |
| `sse_open` / `sse_close` | **multi-shot** SSE: open → many events → one close | this is the TON Connect relay |
| `storage_get/set/remove/clear` | async key/value | walletkit awaits every operation, so all four are tokened |
| `log` | diagnostics | |

`host/reference_host.{h,cpp}` is a complete, working implementation to read.

## 5. Proving a binding works

A new binding is done when it reproduces what the reference one does. The C#
harness (`bindings/csharp/test/InteropSmoke`) is the model; it checks, in order:

1. a round trip, and that **UTF-8** survives it (`приветلاسلام😀`);
2. an unknown method **faults** rather than returning an envelope;
3. 50 concurrent requests each get **their own** result;
4. an unsolicited event reaches the event handler;
5. the HTTP delegate actually serves walletkit (recorded reply, no network);
6. a storage round trip through the core;
7. the facade: mnemonic → signer → adapter → wallet → balance → transfer → sign,
   plus a watch-only lookup;
8. **restoring the same mnemonic yields the same address** — the one assertion that
   catches a broken crypto or encoding path.

Because the fixture reply is recorded, the expected values are fixed: balance
`110576459116021734`, an address that starts `UQ`, a BOC that starts `te6`. A
binding that produces anything else is wrong, not different.

The native suite (`ctest`) covers the core itself and runs the same fixtures, so
a failure there is the core's fault, not the binding's.

## 6. C++ for Telegram Desktop (Qt)

A Qt binding should not look like the C# one translated. Notes for that emitter:

- **DTOs:** `cpp-qt-client` is the one C++ target that fits — `QString`, `QList`,
  `fromJson(QJsonObject)` / `asJsonObject()`. Qt's JSON parser is fast and already
  linked, so no third-party JSON dependency is needed.
- **Async:** tdesktop is callback- and `rpl`-shaped, not future-shaped. Emitting
  `Fn<void(Result)>` completions is the honest primitive; an `rpl::producer`
  adapter on top is a few lines. `QFuture` would fit Qt generally but not tdesktop.
- **Threading:** deliver completions on the main thread (`crl::on_main`) rather
  than the receive thread, and guard callbacks with `base::has_weak_ptr` /
  `crl::guard` so a destroyed consumer cannot be called back. This is the same
  rule as §3, expressed in tdesktop's vocabulary.
- **Events:** a `QObject` emitting signals for TON Connect requests is the natural
  shape, and gives queued delivery to the main thread for free.
- **Delegates:** tdesktop already has HTTP and a local encrypted store; wire
  `http_request` to its networking and `storage_*` to its storage rather than
  pulling in a second stack. SSE needs a streaming reply — `QNetworkReply`'s
  `readyRead` is enough.
- **Keep Qt out of the core.** The core stays plain C++17 with no Qt dependency;
  the Qt-ness lives entirely in the binding, so iOS and Android are unaffected.
