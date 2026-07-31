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
```sh
git clone --recurse-submodules <url>
cmake -B build -S .
cmake --build build
```

## Status
Early scaffolding — see `ROADMAP.md`. Milestone **M0** (walking skeleton) in progress.

## License
TBD.
