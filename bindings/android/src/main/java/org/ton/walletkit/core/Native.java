//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

package org.ton.walletkit.core;

/**
 * The raw C ABI, one static method per entry point.
 *
 * <p>Everything is {@code byte[]} rather than {@code String}: JNI's
 * {@code GetStringUTFChars} produces modified UTF-8, which silently corrupts
 * anything outside the BMP — an emoji in a dapp name would arrive mangled.
 * Callers encode and decode with {@link java.nio.charset.StandardCharsets#UTF_8}.
 *
 * <p>Package-private on purpose: applications use {@link WalletKitClient}.
 */
final class Native {

    private Native() {
    }

    /**
     * Loads the library. Called once, before anything else here.
     *
     * <p>An app that already loads its own natives (Telegram Android loads
     * libtmessages) may prefer to load {@code twk_jni} itself; this is only the
     * default path.
     */
    static void load() {
        System.loadLibrary("twk_jni");
    }

    // ---- host bridge -------------------------------------------------------

    /** Wraps a host object into a native delegate block; returns its handle. */
    static native long delegatesCreate(Object host);

    /** Frees the block. Only safe once the client using it has been destroyed. */
    static native void delegatesDestroy(long handle);

    // ---- client ------------------------------------------------------------

    static native long clientCreate(long delegates);

    static native void clientDestroy(long client);

    static native void send(long client, long requestId, byte[] method, byte[] params);

    /**
     * Blocks for up to {@code timeout} seconds.
     *
     * @param requestIdOut a one-element array receiving the id; 0 means an
     *                     unsolicited update, which answers no request
     * @return the message, or null on timeout
     */
    static native byte[] receive(long client, double timeout, long[] requestIdOut);

    // ---- completions -------------------------------------------------------
    // Called from whatever thread the host finished its work on.

    static native void httpRespond(long client, long token, int status, byte[] headersJson, byte[] body);

    static native void httpFailed(long client, long token, byte[] error);

    static native void sseEvent(long client, long token, byte[] data);

    static native void sseClosed(long client, long token, byte[] error);

    static native void storageRespond(long client, long token, byte[] value);
}
