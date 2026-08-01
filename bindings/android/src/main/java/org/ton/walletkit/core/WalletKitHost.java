package org.ton.walletkit.core;

/**
 * What the core needs from the app: network, streaming and storage.
 *
 * <p>Deliberately callback-shaped rather than future-shaped. {@code
 * CompletableFuture} needs API 24 and is not reliably desugared, and Telegram
 * Android supports 21 — callbacks also match how that codebase already works.
 *
 * <p>Every method is called on the core's worker thread and must return
 * promptly: do the work elsewhere and call the completion when it finishes.
 * Completions may arrive on any thread, and may arrive after the client is
 * closed — the core tolerates that.
 */
public interface WalletKitHost {

    /** Network. Telegram Android would route this through its own stack. */
    interface Http {
        /**
         * Performs one request.
         *
         * @param headersJson a flat JSON object of request headers
         * @param body        the request body, or null
         * @param completion  answer exactly once
         */
        void send(String method, String url, String headersJson, String body, HttpCompletion completion);

        /** The core no longer wants the answer. Best-effort. */
        void cancel(long token);
    }

    /** Answers one HTTP request. Exactly one of these, exactly once. */
    interface HttpCompletion {
        void respond(int status, String headersJson, String body);

        void fail(String error);
    }

    /**
     * Server-sent events — the TON Connect relay.
     *
     * <p>Unlike HTTP this is multi-shot: one open, many events, one close.
     */
    interface Sse {
        void open(String url, String headersJson, SseSink sink);

        void close(long token);
    }

    /** Receives one stream's events. */
    interface SseSink {
        /** One event, as a JSON object with data/event/id. */
        void event(String json);

        /** The stream ended. {@code error} is null for a clean close. */
        void closed(String error);
    }

    /**
     * Key/value storage.
     *
     * <p>This holds TON Connect session private keys, so it must be encrypted at
     * rest — on Android, the Keystore or an app's existing encrypted store.
     */
    interface Storage {
        /** Answer with null when the key is absent. */
        void get(String key, StorageCompletion completion);

        void set(String key, String value, StorageCompletion completion);

        void remove(String key, StorageCompletion completion);

        void clear(StorageCompletion completion);
    }

    /** Answers one storage operation. */
    interface StorageCompletion {
        void respond(String value);
    }

    Http http();

    /** May be null: without it TON Connect cannot receive dapp requests. */
    Sse sse();

    Storage storage();

    /** Diagnostics from the core and from the JS it runs. */
    void log(int level, String message);
}
