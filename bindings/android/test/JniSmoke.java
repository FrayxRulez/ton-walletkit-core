//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// Drives the Java binding against a real libtwk_jni, on a desktop JVM.
//
// The JNI layer is plain JNI — nothing about it is Android-specific — so running
// it here catches the things that actually go wrong (UTF-8 handling, delegate
// callbacks from the core's own thread, teardown ordering) without an emulator.
// The emulator run in CI then confirms packaging rather than logic.
//
//   javac -d out bindings/android/src/main/java/org/ton/walletkit/core/*.java \
//         bindings/android/test/JniSmoke.java
//   java -cp out -Djava.library.path=build-amd64/bin JniSmoke
import java.io.IOException;
import java.nio.charset.Charset;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

import org.ton.walletkit.core.WalletKitClient;
import org.ton.walletkit.core.WalletKitException;
import org.ton.walletkit.core.WalletKitHost;

public final class JniSmoke {

    private static int failures = 0;

    private static void check(String name, boolean ok, String detail) {
        System.out.println((ok ? "ok" : "FAIL") + ": " + name + (detail == null ? "" : " -> " + truncate(detail)));
        if (!ok) {
            failures++;
        }
    }

    private static String truncate(String value) {
        if (value == null) {
            return "(null)";
        }
        return value.length() > 90 ? value.substring(0, 90) : value;
    }

    /** One blocking call, so the test reads top to bottom. */
    private static String call(WalletKitClient client, String method, String params) throws Exception {
        final AtomicReference<String> result = new AtomicReference<String>();
        final AtomicReference<WalletKitException> failure = new AtomicReference<WalletKitException>();
        final CountDownLatch done = new CountDownLatch(1);

        client.send(method, params, new WalletKitClient.ResultHandler() {
            @Override
            public void onResult(String value, WalletKitException error) {
                result.set(value);
                failure.set(error);
                done.countDown();
            }
        });

        if (!done.await(60, TimeUnit.SECONDS)) {
            throw new IllegalStateException("timed out: " + method);
        }
        if (failure.get() != null) {
            throw failure.get();
        }
        return result.get();
    }

    public static void main(String[] args) throws Exception {
        // The console is not UTF-8 on Windows, and this test prints an emoji.
        System.setOut(new java.io.PrintStream(new java.io.FileOutputStream(java.io.FileDescriptor.out),
                true, "UTF-8"));

        Golden golden = Golden.load();
        FakeHost host = new FakeHost(golden);
        WalletKitClient client = new WalletKitClient(host);

        final AtomicInteger updates = new AtomicInteger();
        client.setUpdateHandler(new WalletKitClient.UpdateHandler() {
            @Override
            public void onUpdate(String update) {
                updates.incrementAndGet();
            }
        });

        try {
            // 1. Round trip through JNI and back.
            String echo = call(client, "echo", "[\"hello from Java\"]");
            check("round trip", echo != null && echo.contains("hello from Java"), echo);

            // 2. The reason nothing here uses jstring: modified UTF-8 would mangle
            //    the emoji, and it would do so silently.
            String unicode = golden.string("expected", "unicodeEcho");
            String utf8 = call(client, "echo", "[\"" + unicode + "\"]");
            check("utf-8 survives JNI", utf8 != null && utf8.contains(unicode), utf8);

            // 3. Errors arrive as an exception, not as an envelope to parse.
            try {
                call(client, "noSuchMethod", "[]");
                check("error reaches the handler", false, "no exception");
            } catch (WalletKitException error) {
                check("error reaches the handler",
                        error.getMessage().contains(golden.string("expected", "unknownMethodError")),
                        error.getMessage());
            }

            // 4. Correlation: many in flight, each answered with its own result.
            final int count = 50;
            final CountDownLatch done = new CountDownLatch(count);
            final boolean[] correlated = { true };
            for (int i = 0; i < count; i++) {
                final int n = i;
                client.send("echo", "[" + n + "]", new WalletKitClient.ResultHandler() {
                    @Override
                    public void onResult(String value, WalletKitException error) {
                        if (value == null || !value.contains("\"result\":" + n)) {
                            correlated[0] = false;
                        }
                        done.countDown();
                    }
                });
            }
            check(count + " concurrent requests each got their own result",
                    done.await(60, TimeUnit.SECONDS) && correlated[0], null);

            // 5. Unsolicited updates reach the update handler.
            call(client, "initWalletKit", "[{\"networks\":[{\"chainId\":\"-3\"}]}]");
            call(client, "emitTestEvent", "[\"connectRequest\",{\"id\":\"x\"}]");
            for (int i = 0; i < 50 && updates.get() == 0; i++) {
                Thread.sleep(20);
            }
            check("unsolicited update delivered", updates.get() > 0, updates.get() + " update(s)");

            // 6. The HTTP delegate: the core calls Java from its own thread.
            call(client, "initWalletKit",
                    "[{\"networks\":[{\"chainId\":\"-3\",\"endpoint\":\"https://testnet.toncenter.com\"}]}]");
            String balance = call(client, "getAddressBalance",
                    "[\"-1:3333333333333333333333333333333333333333333333333333333333333333\",\"-3\"]");
            check("http delegate served walletkit",
                    balance != null && balance.contains(golden.string("expected", "balanceNanotons")), balance);
            check("host saw the request", host.requests.size() > 0
                    && host.requests.get(0).contains("addressInformation"),
                    host.requests.isEmpty() ? "(none)" : host.requests.get(0));

            // 7. Storage round trip through the delegate.
            call(client, "storageSet", "[\"k\",\"v\"]");
            String stored = call(client, "storageGet", "[\"k\"]");
            check("storage delegate round trip", stored != null && stored.contains("\"value\":\"v\""), stored);

            // 8. The golden derivation: the same mnemonic must give the same
            //    address here as in every other binding.
            StringBuilder words = new StringBuilder();
            for (String word : golden.strings("mnemonic")) {
                words.append(words.length() == 0 ? "" : ",").append('"').append(word).append('"');
            }
            String signer = call(client, "createSignerFromMnemonic", "[[" + words + "],\"ton\"]");
            check("signer derives the golden public key",
                    signer != null && signer.contains(golden.string("derived", "publicKey")), signer);

            String adapter = call(client, "createV5R1WalletAdapter",
                    "[\"signer:1\",{\"network\":{\"chainId\":\"-3\"}}]");
            check("adapter derives the golden address",
                    adapter != null && adapter.contains(golden.string("derived", "address")), adapter);
        } finally {
            client.close();
        }

        System.out.println(failures == 0 ? "PASS" : "FAILED");
        System.exit(failures == 0 ? 0 : 1);
    }

    // ---- test doubles ------------------------------------------------------

    /** The shared expectations from test/conformance/golden.json. */
    static final class Golden {
        private final String json;

        private Golden(String json) {
            this.json = json;
        }

        static Golden load() throws IOException {
            Path directory = Paths.get("").toAbsolutePath();
            while (directory != null && !Files.exists(directory.resolve("test/conformance/golden.json"))) {
                directory = directory.getParent();
            }
            if (directory == null) {
                throw new IOException("test/conformance/golden.json not found");
            }
            return new Golden(new String(Files.readAllBytes(directory.resolve("test/conformance/golden.json")),
                    Charset.forName("UTF-8")));
        }

        /**
         * Reads one string value by key, honouring backslash escapes — the HTTP
         * fixture is JSON *inside* a JSON string, so stopping at the first quote
         * would hand walletkit a truncated body. Enough for this file's shape,
         * and avoids making the test depend on a JSON library the binding does
         * not use.
         */
        String string(String... path) {
            String key = "\"" + path[path.length - 1] + "\"";
            int at = json.indexOf(key);
            if (at < 0) {
                throw new IllegalArgumentException("missing: " + key);
            }

            int i = json.indexOf('"', json.indexOf(':', at) + 1) + 1;
            StringBuilder value = new StringBuilder();
            while (i < json.length()) {
                char c = json.charAt(i);
                if (c == '\\') {
                    char next = json.charAt(i + 1);
                    value.append(next == 'n' ? '\n' : next == 't' ? '\t' : next);
                    i += 2;
                    continue;
                }
                if (c == '"') {
                    break;
                }
                value.append(c);
                i++;
            }
            return value.toString();
        }

        List<String> strings(String name) {
            int at = json.indexOf("\"" + name + "\"");
            int start = json.indexOf('[', at);
            int end = json.indexOf(']', start);
            List<String> values = new ArrayList<String>();
            for (String part : json.substring(start + 1, end).split(",")) {
                String trimmed = part.trim();
                if (trimmed.length() > 1) {
                    values.add(trimmed.substring(1, trimmed.length() - 1));
                }
            }
            return values;
        }
    }

    /** Answers every request from the golden fixture, so this needs no network. */
    static final class FakeHost implements WalletKitHost, WalletKitHost.Http, WalletKitHost.Storage {
        final List<String> requests = new ArrayList<String>();
        private final java.util.Map<String, String> store = new java.util.HashMap<String, String>();
        private final Golden golden;

        FakeHost(Golden golden) {
            this.golden = golden;
        }

        @Override
        public Http http() {
            return this;
        }

        @Override
        public Sse sse() {
            return null;
        }

        @Override
        public Storage storage() {
            return this;
        }

        @Override
        public void log(int level, String message) {
        }

        @Override
        public void send(String method, String url, String headersJson, String body,
                         final HttpCompletion completion) {
            synchronized (requests) {
                requests.add(url);
            }
            // On a worker thread, as a real host would be — which is also what
            // exercises completing from a thread the core does not own.
            new Thread(new Runnable() {
                @Override
                public void run() {
                    completion.respond(200, "{\"content-type\":\"application/json\"}",
                            golden.string("httpFixture", "body"));
                }
            }).start();
        }

        @Override
        public void cancel(long token) {
        }

        @Override
        public void get(String key, StorageCompletion completion) {
            synchronized (store) {
                completion.respond(store.get(key));
            }
        }

        @Override
        public void set(String key, String value, StorageCompletion completion) {
            synchronized (store) {
                store.put(key, value);
            }
            completion.respond(null);
        }

        @Override
        public void remove(String key, StorageCompletion completion) {
            synchronized (store) {
                store.remove(key);
            }
            completion.respond(null);
        }

        @Override
        public void clear(StorageCompletion completion) {
            synchronized (store) {
                store.clear();
            }
            completion.respond(null);
        }
    }
}
