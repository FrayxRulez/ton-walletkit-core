#!/usr/bin/env sh
# Configure + build (+ optionally test) the core on Linux/macOS.
# The POSIX counterpart of win-build.bat.
#
#   scripts/build.sh              build
#   scripts/build.sh test         build and run the suite
#   TWK_CRYPTO_BACKEND=portable scripts/build.sh test
#
# The JS bundle must exist first — the native build embeds it:
#   (cd js && npm ci && npm run build)
set -eu

action="${1:-build}"
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${TWK_BUILD_DIR:-$root/build}"
backend="${TWK_CRYPTO_BACKEND:-auto}"

if [ ! -f "$root/js/dist/bundle.h" ]; then
    echo "[build] js/dist/bundle.h missing — run 'npm ci && npm run build' in js/ first" >&2
    exit 1
fi

cmake -B "$build" -S "$root" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTWK_CRYPTO_BACKEND="$backend"
cmake --build "$build" --parallel

if [ "$action" = "test" ]; then
    ctest --test-dir "$build" --output-on-failure
fi

echo "[build] done ($action, crypto backend $backend)"
