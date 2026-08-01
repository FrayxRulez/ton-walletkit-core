#!/usr/bin/env sh
#
# Copyright (c) Fela Ameghino 2026
#
# Distributed under the MIT License. (See accompanying file LICENSE or copy at
# https://opensource.org/licenses/MIT)
#
# Cross-builds libtwk.so for Android, one ABI at a time.
#
#   ANDROID_NDK_HOME=/path/to/ndk scripts/android-build.sh              # all ABIs
#   ANDROID_NDK_HOME=/path/to/ndk scripts/android-build.sh arm64-v8a    # just one
#
# The settings match Telegram Android (TMessagesProj/build.gradle) so the result
# can be dropped into the same app:
#
#   NDK r27          ndkVersion "27.2.12479018"
#   android-21       minSdkVersion 21
#   c++_static       -DANDROID_STL=c++_static — mixing STLs in one process is a
#                    crash source, so this has to match whatever the app uses
#
# The JS bundle must be built first (js: npm ci && npm run build), and the
# bytecode step needs a *host* twk-bundlec — build for the host once, then pass
# TWK_BUNDLEC, or the bundle is embedded as source and startup costs ~280ms more.
set -eu

ndk="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
if [ -z "$ndk" ]; then
    echo "[android] set ANDROID_NDK_HOME (or ANDROID_NDK_ROOT) to an NDK r27 install" >&2
    exit 1
fi

root="$(cd "$(dirname "$0")/.." && pwd)"
abis="${*:-arm64-v8a armeabi-v7a x86_64 x86}"
platform="${ANDROID_PLATFORM:-android-21}"
stl="${ANDROID_STL:-c++_static}"
out="${TWK_ANDROID_OUT:-$root/build-android}"

bundlec="${TWK_BUNDLEC:-}"
if [ -z "$bundlec" ]; then
    for candidate in "$root/build/bin/twk-bundlec" "$root/build-amd64/bin/twk-bundlec.exe"; do
        [ -x "$candidate" ] && bundlec="$candidate" && break
    done
fi

for abi in $abis; do
    echo "[android] $abi ($platform, $stl)"
    cmake -B "$out/$abi" -S "$root" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$abi" \
        -DANDROID_PLATFORM="$platform" \
        -DANDROID_STL="$stl" \
        -DCMAKE_BUILD_TYPE=Release \
        ${bundlec:+-DTWK_BUNDLEC="$bundlec"}
    cmake --build "$out/$abi" --target twk_shared

    mkdir -p "$root/android/jniLibs/$abi"
    cp "$out/$abi/src/libtwk.so" "$root/android/jniLibs/$abi/libtwk.so"
done

echo "[android] libraries in android/jniLibs/<abi>/libtwk.so"
