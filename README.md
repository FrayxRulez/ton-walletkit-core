# ton-walletkit-core

A cross-platform C++ core that runs the canonical [`@ton/walletkit`](https://github.com/ton-org/kit) JS in
an embedded [QuickJS-ng](https://github.com/quickjs-ng/quickjs) engine and exposes a small **JSON `send`/
`receive` C ABI** (+ updates). One core serves Windows, iOS, Android, macOS and Linux; each platform
supplies a thin binding and a few host delegates (HTTP, SSE, secure storage, RNG).

- **Design:** [`ARCHITECTURE.md`](ARCHITECTURE.md)
- **Plan:** [`ROADMAP.md`](ROADMAP.md) — milestones M0…M8

## Layout
```
include/twk/     public C ABI (twk.h, twk_delegates.h)
src/             the core: engine (QuickJS host + event loop), shims, bridge
js/              the JS QuickJS runs: ported walletkit bridge + polyfills (built to an embedded bundle)
bindings/        thin per-language wrappers (csharp / swift / kotlin)
tools/twk-cli/   desktop test driver: JSON lines on stdin -> send -> print receive
test/            unit + integration tests
third_party/     vendored deps (QuickJS-ng submodule)
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

## Status
Early scaffolding — see `ROADMAP.md`. Milestone **M0** (walking skeleton) in progress.

## License
TBD.
