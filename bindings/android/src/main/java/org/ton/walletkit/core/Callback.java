package org.ton.walletkit.core;

/**
 * The answer to one call. Exactly one argument is non-null.
 *
 * <p>Callbacks rather than futures throughout: CompletableFuture needs API 24
 * and is not reliably desugared, and Telegram Android supports 21.
 *
 * <p>Called on the receive thread, so anything touching UI must post to its own
 * looper first.
 */
public interface Callback<T> {
    void onResult(T result, WalletKitException error);
}
