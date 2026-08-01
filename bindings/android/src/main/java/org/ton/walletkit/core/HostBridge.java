//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

package org.ton.walletkit.core;

import java.nio.charset.Charset;

/**
 * The object the JNI layer calls into.
 *
 * <p>Its method names and signatures are resolved by name in twk_jni.cpp, so
 * renaming one breaks the binding at runtime rather than at compile time. It
 * exists to keep those signatures out of {@link WalletKitHost}, which is the
 * interface applications actually implement.
 */
final class HostBridge {

    // StandardCharsets needs API 19; Telegram supports 21, so this is fine — but
    // Charset.forName keeps the binding usable further back if that ever matters.
    static final Charset UTF_8 = Charset.forName("UTF-8");

    private final WalletKitHost host;

    HostBridge(WalletKitHost host) {
        this.host = host;
    }

    static String string(byte[] value) {
        return value == null ? null : new String(value, UTF_8);
    }

    static byte[] bytes(String value) {
        return value == null ? null : value.getBytes(UTF_8);
    }

    // ---- called from the core's worker thread ------------------------------

    void onHttpRequest(final long client, final long token, byte[] method, byte[] url, byte[] headersJson,
                       byte[] body) {
        WalletKitHost.Http http = host == null ? null : host.http();
        if (http == null) {
            Native.httpFailed(client, token, bytes("no http host"));
            return;
        }

        http.send(token, string(method), string(url), string(headersJson), string(body),
                new WalletKitHost.HttpCompletion() {
                    @Override
                    public void respond(int status, String headersJson, String body) {
                        Native.httpRespond(client, token, status, bytes(headersJson), bytes(body));
                    }

                    @Override
                    public void fail(String error) {
                        Native.httpFailed(client, token, bytes(error));
                    }
                });
    }

    void onHttpCancel(long client, long token) {
        WalletKitHost.Http http = host == null ? null : host.http();
        if (http != null) {
            http.cancel(token);
        }
    }

    void onSseOpen(final long client, final long token, byte[] url, byte[] headersJson) {
        WalletKitHost.Sse sse = host == null ? null : host.sse();
        if (sse == null) {
            Native.sseClosed(client, token, bytes("no sse host"));
            return;
        }

        sse.open(token, string(url), string(headersJson), new WalletKitHost.SseSink() {
            @Override
            public void event(String json) {
                Native.sseEvent(client, token, bytes(json));
            }

            @Override
            public void closed(String error) {
                Native.sseClosed(client, token, bytes(error));
            }
        });
    }

    void onSseClose(long client, long token) {
        WalletKitHost.Sse sse = host == null ? null : host.sse();
        if (sse != null) {
            sse.close(token);
        }
    }

    void onStorageGet(final long client, final long token, byte[] key) {
        WalletKitHost.Storage storage = host == null ? null : host.storage();
        if (storage == null) {
            Native.storageRespond(client, token, null);
            return;
        }
        storage.get(string(key), completion(client, token));
    }

    void onStorageSet(final long client, final long token, byte[] key, byte[] value) {
        WalletKitHost.Storage storage = host == null ? null : host.storage();
        if (storage == null) {
            Native.storageRespond(client, token, null);
            return;
        }
        storage.set(string(key), string(value), completion(client, token));
    }

    void onStorageRemove(final long client, final long token, byte[] key) {
        WalletKitHost.Storage storage = host == null ? null : host.storage();
        if (storage == null) {
            Native.storageRespond(client, token, null);
            return;
        }
        storage.remove(string(key), completion(client, token));
    }

    void onStorageClear(final long client, final long token) {
        WalletKitHost.Storage storage = host == null ? null : host.storage();
        if (storage == null) {
            Native.storageRespond(client, token, null);
            return;
        }
        storage.clear(completion(client, token));
    }

    void onLog(long client, int level, byte[] message) {
        if (host != null) {
            host.log(level, string(message));
        }
    }

    private static WalletKitHost.StorageCompletion completion(final long client, final long token) {
        return new WalletKitHost.StorageCompletion() {
            @Override
            public void respond(String value) {
                Native.storageRespond(client, token, bytes(value));
            }
        };
    }
}
