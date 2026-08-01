#!/usr/bin/env sh
#
# Copyright (c) Fela Ameghino 2026
#
# Distributed under the MIT License. (See accompanying file LICENSE or copy at
# https://opensource.org/licenses/MIT)
#
# Cross-builds libtwk_jni.so for Android, one ABI at a time, and stages it where
# the Gradle module packages it into the AAR.
#
#   ANDROID_NDK_HOME=/path/to/ndk scripts/android-build.sh              # all ABIs
#   ANDROID_NDK_HOME=/path/to/ndk scripts/android-build.sh arm64-v8a    # just one
#
# One library per ABI: the core is linked into the JNI shim statically, so an app
# calls System.loadLibrary("twk_jni") and nothing else. Gradle does no NDK work —
# it packages what this produces — so assembling the AAR needs only a JDK and the
# Android SDK.
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
# The three that matter: two device ABIs and the emulator. x86 still works if
# asked for by name, but no current device needs it and it only grows the AAR.
abis="${*:-arm64-v8a armeabi-v7a x86_64}"
platform="${ANDROID_PLATFORM:-android-21}"
stl="${ANDROID_STL:-c++_static}"
out="${TWK_ANDROID_OUT:-$root/build-android}"

jni_libs="${TWK_JNI_LIBS:-$root/bindings/android/src/main/jniLibs}"

# llvm-strip ships in the NDK's prebuilt toolchain; the host triple varies.
strip=""
for candidate in "$ndk"/toolchains/llvm/prebuilt/*/bin/llvm-strip; do
    [ -x "$candidate" ] && strip="$candidate" && break
done

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
    cmake --build "$out/$abi" --target twk_jni

    mkdir -p "$jni_libs/$abi"
    cp "$out/$abi/bin/libtwk_jni.so" "$jni_libs/$abi/libtwk_jni.so"

    # Stripping is most of the file: ~13 MB of DWARF for ~3 MB of code.
    if [ -n "$strip" ]; then
        "$strip" --strip-unneeded "$jni_libs/$abi/libtwk_jni.so"
    fi
    echo "[android] $abi -> $(du -h "$jni_libs/$abi/libtwk_jni.so" | cut -f1)"
done

echo "[android] libraries in bindings/android/src/main/jniLibs/<abi>/libtwk_jni.so"
echo "[android] now: cd bindings/android && gradle assembleRelease"
