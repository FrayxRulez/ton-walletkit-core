//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

package org.ton.walletkit.core;

import android.content.Context;
import android.content.SharedPreferences;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * A {@link WalletKitHost} on plain Android APIs: {@code HttpURLConnection},
 * a reader thread per SSE stream, and {@code SharedPreferences}.
 *
 * <p>This exists so the binding runs out of the box and so the contract has a
 * worked example. An app with its own networking should implement
 * {@link WalletKitHost} against that instead — Telegram Android would route HTTP
 * through its own stack, and the Unigram binding routes it through TDLib.
 *
 * <p><b>Storage is not encrypted.</b> {@code SharedPreferences} is world-readable
 * to the app only, which is not the same as encrypted at rest, and what goes in
 * here includes TON Connect session private keys. Replace {@link #storage()}
 * before shipping — see the class comment on {@link WalletKitHost.Storage}.
 */
public class AndroidWalletKitHost implements WalletKitHost {

    private static final int CONNECT_TIMEOUT_MS = 15000;
    private static final int READ_TIMEOUT_MS = 30000;

    /** A stream is only read in chunks this long so close() is noticed promptly. */
    private static final int SSE_READ_TIMEOUT_MS = 5000;

    private final SharedPreferences preferences;
    private final ExecutorService executor;

    private final Map<Long, Future<?>> requests = new ConcurrentHashMap<Long, Future<?>>();
    private final Map<Long, Stream> streams = new ConcurrentHashMap<Long, Stream>();

    private final Http http = new HttpImpl();
    private final Sse sse = new SseImpl();
    private final Storage storage = new StorageImpl();

    public AndroidWalletKitHost(Context context) {
        this(context, "ton_walletkit");
    }

    /** @param name the SharedPreferences file; one per account keeps them apart. */
    public AndroidWalletKitHost(Context context, String name) {
        this.preferences = context.getApplicationContext().getSharedPreferences(name, Context.MODE_PRIVATE);
        // Cached, not fixed: an SSE stream holds its thread for the life of the
        // stream, so a bounded pool would let one relay starve every HTTP call.
        this.executor = Executors.newCachedThreadPool(new ThreadFactory() {
            @Override
            public Thread newThread(Runnable runnable) {
                Thread thread = new Thread(runnable, "twk-host");
                thread.setDaemon(true);
                return thread;
            }
        });
    }

    /** Stops the worker threads. Call after the client is closed, not before. */
    public void close() {
        for (Stream stream : streams.values()) {
            stream.cancel();
        }
        streams.clear();
        requests.clear();
        executor.shutdownNow();
    }

    @Override
    public Http http() {
        return http;
    }

    @Override
    public Sse sse() {
        return sse;
    }

    @Override
    public Storage storage() {
        return storage;
    }

    @Override
    public void log(int level, String message) {
        // Deliberately silent: the core logs the JS console, which includes
        // request bodies. Override to route it somewhere.
    }

    // ---- helpers ----------------------------------------------------------

    private static void applyHeaders(HttpURLConnection connection, String headersJson) {
        if (headersJson == null || headersJson.isEmpty()) {
            return;
        }
        JsonElement parsed;
        try {
            parsed = JsonParser.parseString(headersJson);
        } catch (RuntimeException malformed) {
            return;
        }
        if (!parsed.isJsonObject()) {
            return;
        }
        for (Map.Entry<String, JsonElement> entry : parsed.getAsJsonObject().entrySet()) {
            JsonElement value = entry.getValue();
            if (value != null && value.isJsonPrimitive()) {
                connection.setRequestProperty(entry.getKey(), value.getAsString());
            }
        }
    }

    private static String headersToJson(HttpURLConnection connection) {
        JsonObject headers = new JsonObject();
        for (Map.Entry<String, java.util.List<String>> entry : connection.getHeaderFields().entrySet()) {
            // The status line comes back under a null key.
            if (entry.getKey() != null && entry.getValue() != null && !entry.getValue().isEmpty()) {
                headers.addProperty(entry.getKey().toLowerCase(java.util.Locale.US), entry.getValue().get(0));
            }
        }
        return headers.toString();
    }

    private static String readAll(InputStream stream) throws IOException {
        if (stream == null) {
            return "";
        }
        StringBuilder out = new StringBuilder();
        char[] chunk = new char[4096];
        InputStreamReader reader = new InputStreamReader(stream, HostBridge.UTF_8);
        try {
            int read;
            while ((read = reader.read(chunk)) > 0) {
                out.append(chunk, 0, read);
            }
        } finally {
            reader.close();
        }
        return out.toString();
    }

    private static String describe(Throwable failure) {
        String message = failure.getMessage();
        return message != null && !message.isEmpty() ? message : failure.getClass().getSimpleName();
    }

    // ---- HTTP -------------------------------------------------------------

    private final class HttpImpl implements Http {

        @Override
        public void send(final long token, final String method, final String url, final String headersJson,
                         final String body, final HttpCompletion completion) {
            Future<?> future;
            try {
                future = executor.submit(new Runnable() {
                    @Override
                    public void run() {
                        perform(token, method, url, headersJson, body, completion);
                    }
                });
            } catch (RuntimeException rejected) { // shut down under us
                completion.fail(describe(rejected));
                return;
            }
            requests.put(token, future);
        }

        private void perform(long token, String method, String url, String headersJson, String body,
                             HttpCompletion completion) {
            HttpURLConnection connection = null;
            try {
                connection = (HttpURLConnection) new URL(url).openConnection();
                connection.setRequestMethod(method == null || method.isEmpty() ? "GET" : method);
                connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
                connection.setReadTimeout(READ_TIMEOUT_MS);
                connection.setInstanceFollowRedirects(true);
                applyHeaders(connection, headersJson);

                if (body != null) {
                    connection.setDoOutput(true);
                    byte[] payload = body.getBytes(HostBridge.UTF_8);
                    connection.setFixedLengthStreamingMode(payload.length);
                    OutputStream out = connection.getOutputStream();
                    try {
                        out.write(payload);
                    } finally {
                        out.close();
                    }
                }

                int status = connection.getResponseCode();
                // 4xx/5xx bodies arrive on the error stream, and walletkit reads
                // them: they carry the API's own error description.
                InputStream stream = status >= 400 ? connection.getErrorStream() : connection.getInputStream();
                String responseHeaders = headersToJson(connection);
                String responseBody = readAll(stream);

                if (requests.remove(token) == null) {
                    return; // cancelled while in flight; the core is not listening
                }
                completion.respond(status, responseHeaders, responseBody);
            } catch (Throwable failure) {
                if (requests.remove(token) != null) {
                    completion.fail(describe(failure));
                }
            } finally {
                if (connection != null) {
                    connection.disconnect();
                }
            }
        }

        @Override
        public void cancel(long token) {
            Future<?> future = requests.remove(token);
            if (future != null) {
                future.cancel(true);
            }
        }
    }

    // ---- SSE --------------------------------------------------------------

    /** One relay stream: a thread reading until closed, cancelled by a flag. */
    private final class Stream implements Runnable {

        private final long token;
        private final String url;
        private final String headersJson;
        private final SseSink sink;
        private final AtomicBoolean cancelled = new AtomicBoolean(false);

        private volatile HttpURLConnection connection;

        Stream(long token, String url, String headersJson, SseSink sink) {
            this.token = token;
            this.url = url;
            this.headersJson = headersJson;
            this.sink = sink;
        }

        void cancel() {
            cancelled.set(true);
            // Not disconnect(): tearing the connection down from another thread
            // while the reader is inside read() is where native crashes come
            // from. The read timeout below makes the reader notice the flag.
            HttpURLConnection open = connection;
            if (open != null) {
                open.setReadTimeout(1);
            }
        }

        @Override
        public void run() {
            String error = null;
            try {
                HttpURLConnection open = (HttpURLConnection) new URL(url).openConnection();
                connection = open;
                open.setRequestMethod("GET");
                open.setConnectTimeout(CONNECT_TIMEOUT_MS);
                open.setReadTimeout(SSE_READ_TIMEOUT_MS);
                open.setRequestProperty("Accept", "text/event-stream");
                open.setRequestProperty("Cache-Control", "no-cache");
                applyHeaders(open, headersJson);

                int status = open.getResponseCode();
                if (status < 200 || status >= 300) {
                    error = "sse: HTTP " + status;
                } else {
                    read(open);
                }
            } catch (Throwable failure) {
                if (!cancelled.get()) {
                    error = describe(failure);
                }
            } finally {
                HttpURLConnection open = connection;
                if (open != null) {
                    open.disconnect();
                }
                streams.remove(token);
                // A cancelled stream was closed by the core, which is not
                // listening for the notification any more.
                if (!cancelled.get()) {
                    sink.closed(error);
                }
            }
        }

        /**
         * The event-stream framing: {@code field: value} lines, a blank line ends
         * one event. Only what TON Connect uses is interpreted (data/event/id);
         * comments and retry are skipped.
         */
        private void read(HttpURLConnection open) throws IOException {
            BufferedReader reader =
                    new BufferedReader(new InputStreamReader(open.getInputStream(), HostBridge.UTF_8));
            try {
                StringBuilder data = new StringBuilder();
                String event = null;
                String id = null;

                while (!cancelled.get()) {
                    String line;
                    try {
                        line = reader.readLine();
                    } catch (java.net.SocketTimeoutException idle) {
                        continue; // no traffic in the window; check cancelled and wait again
                    }
                    if (line == null) {
                        return; // server closed
                    }

                    if (line.isEmpty()) {
                        if (data.length() > 0 || event != null) {
                            JsonObject payload = new JsonObject();
                            payload.addProperty("data", data.toString());
                            if (event != null) {
                                payload.addProperty("event", event);
                            }
                            if (id != null) {
                                payload.addProperty("id", id);
                            }
                            sink.event(payload.toString());
                        }
                        data.setLength(0);
                        event = null;
                        id = null;
                        continue;
                    }

                    if (line.charAt(0) == ':') {
                        continue; // comment, often a keep-alive
                    }

                    int colon = line.indexOf(':');
                    String field = colon < 0 ? line : line.substring(0, colon);
                    String value = colon < 0 ? "" : line.substring(colon + 1);
                    if (value.startsWith(" ")) {
                        value = value.substring(1);
                    }

                    if ("data".equals(field)) {
                        if (data.length() > 0) {
                            data.append('\n');
                        }
                        data.append(value);
                    } else if ("event".equals(field)) {
                        event = value;
                    } else if ("id".equals(field)) {
                        id = value;
                    }
                }
            } finally {
                reader.close();
            }
        }
    }

    private final class SseImpl implements Sse {

        @Override
        public void open(long token, String url, String headersJson, SseSink sink) {
            Stream stream = new Stream(token, url, headersJson, sink);
            streams.put(token, stream);
            try {
                executor.execute(stream);
            } catch (RuntimeException rejected) {
                streams.remove(token);
                sink.closed(describe(rejected));
            }
        }

        @Override
        public void close(long token) {
            Stream stream = streams.remove(token);
            if (stream != null) {
                stream.cancel();
            }
        }
    }

    // ---- storage ----------------------------------------------------------

    private final class StorageImpl implements Storage {

        @Override
        public void get(String key, StorageCompletion completion) {
            completion.respond(preferences.getString(key, null));
        }

        @Override
        public void set(String key, String value, StorageCompletion completion) {
            preferences.edit().putString(key, value).apply();
            completion.respond(null);
        }

        @Override
        public void remove(String key, StorageCompletion completion) {
            preferences.edit().remove(key).apply();
            completion.respond(null);
        }

        @Override
        public void clear(StorageCompletion completion) {
            preferences.edit().clear().apply();
            completion.respond(null);
        }
    }
}
