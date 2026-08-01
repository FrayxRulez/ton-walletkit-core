# ton-walletkit-core

A cross-platform C++ core that runs the canonical [`@ton/walletkit`](https://github.com/ton-org/kit) JS in
an embedded [QuickJS-ng](https://github.com/quickjs-ng/quickjs) engine and exposes a small **JSON `send`/
`receive` C ABI** (+ updates). One core serves Windows, iOS, Android, macOS and Linux; each platform
supplies a thin binding and a few host delegates (HTTP, SSE, secure storage, RNG).

- **Design:** [`ARCHITECTURE.md`](ARCHITECTURE.md)
- **Plan:** [`ROADMAP.md`](ROADMAP.md) — milestones M0…M8
- **API:** [`docs/API.md`](docs/API.md) — the JSON methods the core exposes
- **Porting:** [`docs/BINDINGS.md`](docs/BINDINGS.md) — how to write a binding for another language

## Layout
```
include/twk/     public C ABI (twk.h, twk_delegates.h)
src/             the core: engine (QuickJS host + event loop), shims, bridge
js/              the JS QuickJS runs: ported walletkit bridge + polyfills (built to an embedded bundle)
bindings/        thin per-language wrappers (csharp / swift / kotlin)
tools/twk-cli/   desktop test driver: JSON lines on stdin -> send -> print receive
test/            unit + integration tests
api/facade.json  the typed facade every binding exposes, described once
scripts/         build helpers, the codegens (DTOs + facade), and the upstream tracker
third_party/     vendored deps (QuickJS-ng submodule)
upstream.json    the upstream commits this library was written against
```

## Build (desktop)
Requires **Node ≥ 24** (for the JS bundle), a C++17 toolchain, and CMake.

```sh
git clone --recurse-submodules <url>

# 1. Build the embedded JS bundle -> js/dist/bundle.{js,h}
cd js && npm ci && npm run build && cd ..

# 2. Build + test the native core
cmake -B build -S .
cmake --build build
```

The native build embeds `js/dist/bundle.h`, so step 1 must run first (CI wiring
comes in M8). On Windows, `scripts\win-build.bat amd64 test` does the CMake +
Ninja build and runs the tests under the MSVC toolchain.

## Platform support

The core is plain C++17 with no third-party runtime dependency. What differs per
platform is small and isolated:

| | crypto (SHA-2 / HMAC / PBKDF2) | entropy | test host (HTTP/SSE) |
|---|---|---|---|
| Windows | BCrypt, in-box | `BCryptGenRandom` | WinHTTP |
| macOS / iOS | CommonCrypto, in libSystem¹ | `getentropy` | — |
| Linux desktop | OpenSSL when CMake finds it¹ | `getrandom(2)` | — |
| Android | portable² | `getrandom(2)` | — |
| anything else | portable² | `/dev/urandom` | — |

¹ written but not yet compiled here — `twk_crypto_kat` is the acceptance gate.
² `src/util/crypto_portable.cpp`: a self-contained FIPS 180-4 implementation.

Pick one explicitly with `-DTWK_CRYPTO_BACKEND=bcrypt|commoncrypto|openssl|portable`.
**`twk_crypto_kat` must pass whichever is selected** — it checks the output against
the published RFC vectors plus the TON derivation the mnemonic loop uses, because
a backend that is fast and wrong silently produces wrong keys.

The portable backend exists so no platform is stuck with the pure-JS fallback,
which costs ~49 ms per HMAC in the interpreter (16.7 s to create one mnemonic).
It is not a match for a system implementation, and is not meant to be:

| | portable | BCrypt |
|---|---|---|
| `sha256` | 0.033 ms | 0.033 ms |
| `hmac_sha512` | 0.100 ms | 0.067 ms |
| `pbkdf2` (390 iterations) | 3.87 ms | 0.63 ms |

Entropy is never `std::random_device`, which the standard permits to be a
deterministic PRNG — and on some toolchains really is.

**The shipped core never makes an HTTP request itself** — HTTP and SSE are host
delegates, supplied by the embedder (Unigram routes them through TDLib, Telegram
Desktop would use its own networking, iOS `URLSession`, Android OkHttp). So there
is nothing to port there.

The *test* host is a different matter: it uses WinHTTP on Windows and libcurl
elsewhere when CMake finds it, which is what lets the suite run on Linux/macOS —
and with it the sanitizers MSVC does not have (TSan, UBSan). Without either
backend the host reports `httpAvailable() == false` and the tests that need real
requests skip rather than fail. The libcurl path is written but **not yet
compiled** anywhere.

Build with `scripts/win-build.bat [amd64|arm64] [test]` on Windows, or
`scripts/build.sh [test]` elsewhere.

## Tracking upstream

This library mirrors three upstream repos, and drifts from them silently if nobody
looks. [`upstream.json`](upstream.json) records the exact commit each was last
reviewed at, and why we care about it:

| repo | what we take from it |
|---|---|
| [`ton-org/kit`](https://github.com/ton-org/kit) | the JS we embed: `api/models` is what the C# DTOs are generated from, and `walletkit-ios-bridge` is the model for `js/src` |
| [`ton-connect/kit-ios`](https://github.com/ton-connect/kit-ios) | the API the C# binding mirrors method for method |
| [`ton-connect/kit-android`](https://github.com/ton-connect/kit-android) | the second consumer of the same JS, and the model for our OpenAPI codegen |

```sh
node scripts/upstream.mjs              # what changed since the pins
node scripts/upstream.mjs files kit-ios # which files, for the paths we mirror
node scripts/upstream.mjs diff kit-ios  # the diff itself
node scripts/upstream.mjs pin kit-ios   # move the pin, once absorbed
```

Each entry lists the `paths` we actually mirror, so the report ignores demo apps,
`appkit` and the MCP server, and counts only commits that can affect us. Clones
land in `.upstream/` (gitignored) as blobless partial clones — full history for
`log`/`diff`, blobs fetched only when you ask for a diff.

The pinned npm version of `@ton/walletkit` lives in `js/package.json`;
`upstream.json` repeats it so one file answers "what were we built against".

## Status
**M0–M6 done** (see `ROADMAP.md`): the core runs walletkit in QuickJS behind the C ABI, with
host delegates for HTTP/SSE/storage, native crypto, sanitizer-clean teardown, and a C# binding
that mirrors kit-ios method for method over generated DTOs. Left in M6: arm64 and packaging.

## License
TBD.
