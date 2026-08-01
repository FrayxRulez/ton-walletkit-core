# TON WalletKit — Android binding

`ton-walletkit-release.aar`: the wallet core (`@ton/walletkit` running in QuickJS)
plus a Java 8 API over it. One `libtwk_jni.so` per ABI, with the core linked in
statically, so the app loads one library and nothing else.

Built for **Telegram Android**: `minSdk 21`, Java 8, callbacks rather than
`CompletableFuture` (which needs API 24), NDK r27 with `c++_static`.

## Getting the AAR

Every push builds it. Open the latest green run under
[Actions](https://github.com/FrayxRulez/ton-walletkit-core/actions), and download
the **`ton-walletkit-aar`** artifact — no toolchain needed to consume it.

To build it yourself you need the Android SDK, a JDK, an NDK r27 and Node:

```sh
cd js && npm ci && npm run build && cd ..     # embeds the JS bundle
cmake -B build-host -S . -G Ninja && cmake --build build-host --target twk-bundlec
TWK_BUNDLEC=$PWD/build-host/bin/twk-bundlec \
  ANDROID_NDK_HOME=$ANDROID_SDK_ROOT/ndk/27.2.12479018 \
  scripts/android-build.sh                    # -> src/main/jniLibs/<abi>/
cd bindings/android && gradle assembleRelease # -> build/outputs/aar/
```

Gradle does no NDK work — it packages what the script produced. That is
deliberate: it keeps CMake and Node out of the Gradle build, and it means the
`.so` that was tested is the one that ships.

## Adding it to an app

```gradle
dependencies {
    implementation files('libs/ton-walletkit-release.aar')
}
```

That is the whole integration. **The binding has no dependencies** — the models
read and write themselves through `org.json`, which is part of Android — so
there is no second JSON library in the app and no version to conflict with.

R8 rules ship inside the AAR (`consumer-rules.pro`) and are applied
automatically. They keep two things only: the native method names, which the
`.so` binds by symbol, and the `HostBridge` methods the JNI shim resolves by
name. The models are not among them — nothing reflects over them, so R8 may
rename and shrink the whole `org.ton.walletkit.api` package.

## Using it

```java
WalletKitHost host = new AndroidWalletKitHost(context);

TonWalletKitConfiguration configuration = new TonWalletKitConfiguration()
        .setNetworkConfigurations(networks)
        .setWalletManifest(manifest)
        .setBridge(bridge);        // required for TON Connect

TonWalletKit kit = new TonWalletKit(host, configuration);
kit.initialize(new Callback<Void>() {
    @Override public void onResult(Void ignored) { /* ready */ }
    @Override public void onError(WalletKitException error) { /* … */ }
});
```

Every call is asynchronous and answers on a background thread — post to the main
thread yourself before touching views.

`AndroidWalletKitHost` is a **reference implementation**, on `HttpURLConnection`,
a reader thread per SSE stream and `SharedPreferences`. It exists so the binding
runs out of the box. Two reasons to replace it:

- **Storage is not encrypted.** What goes in it includes TON Connect session
  private keys. Use the Keystore, or the app's existing encrypted store.
- **Networking is separate from the app's.** An app with its own stack should
  implement `WalletKitHost` against that — which is the intended path for
  Telegram Android, and is what the Unigram binding does with TDLib.

Implementing `WalletKitHost` is the whole integration surface: HTTP, SSE and
key/value storage. Every method is called on the core's worker thread and must
return promptly; completions may arrive on any thread, and may arrive after the
client is closed — the core tolerates that.

## Notes for Telegram Android

- **Consume the AAR, not the CMake project.** `TMessagesProj` asks for CMake
  3.10 in its `externalNativeBuild`; this project requires 3.20 and cannot be
  `add_subdirectory`'d into that build. The prebuilt `.so` sidesteps it — no
  CMake runs on their side at all.
- **`c++_static` matches**, so the STL is not shared across libraries and
  `libtwk_jni.so` cannot conflict with `libtmessages`.
- **16 KB page alignment** is on, so it loads on Android 15 devices with 16 KB
  pages.
- **No new dependencies.** The DTOs are generated onto `org.json`, the JSON
  library `TMessagesProj` already uses, rather than onto Gson — which is why
  `scripts/generate-api/emit-java-dto.mjs` exists instead of openapi-generator's
  Java target, since that one only speaks Gson or Jackson.
- The app may prefer to `System.loadLibrary("twk_jni")` itself alongside its own
  natives; `Native.load()` is only the default path.

## Layout

| Path | What |
|---|---|
| `src/main/java/org/ton/walletkit/core/` | transport, host interface, facade |
| `src/main/java/org/ton/walletkit/api/` | generated DTOs, on `org.json` |
| `src/android/java/` | the reference host — the only part needing a `Context` |
| `src/main/cpp/twk_jni.cpp` | the JNI shim |
| `test/JniSmoke.java` | runs the real core on a desktop JVM, no emulator |

`src/main/java` is deliberately free of Android APIs, which is what lets
`JniSmoke` compile and run the binding on a plain JVM — that is how it is tested
in CI on every push.
