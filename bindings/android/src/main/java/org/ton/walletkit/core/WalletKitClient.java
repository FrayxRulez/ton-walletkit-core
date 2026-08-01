package org.ton.walletkit.core;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

/**
 * One core client, and the thread that pumps its messages.
 *
 * <p>Shaped like TDLib's Java client on purpose — {@code send} with a result
 * handler, updates through a separate handler — because that is what a Telegram
 * codebase already expects. Results arrive on the receive thread, so a handler
 * that touches UI must post to its own looper.
 */
public final class WalletKitClient {

    /** Answers one request. Exactly one of the two arguments is non-null. */
    public interface ResultHandler {
        void onResult(String result, WalletKitException error);
    }

    /** Unsolicited updates: TON Connect requests, disconnects. */
    public interface UpdateHandler {
        void onUpdate(String update);
    }

    private static final AtomicBoolean LOADED = new AtomicBoolean();

    private final long delegates;
    private final long client;
    private final Thread receiveThread;
    private final Map<Long, ResultHandler> pending = new ConcurrentHashMap<Long, ResultHandler>();
    private final AtomicLong nextRequestId = new AtomicLong();
    private final AtomicBoolean closed = new AtomicBoolean();

    private volatile boolean running = true;
    private volatile UpdateHandler updateHandler;

    /**
     * Creates a client backed by {@code host}.
     *
     * <p>The library is loaded on first use; an app that loads its own natives
     * can call {@code System.loadLibrary("twk_jni")} beforehand instead.
     */
    public WalletKitClient(WalletKitHost host) {
        if (LOADED.compareAndSet(false, true)) {
            Native.load();
        }

        delegates = Native.delegatesCreate(new HostBridge(host));
        client = Native.clientCreate(delegates);
        if (client == 0) {
            Native.delegatesDestroy(delegates);
            throw new IllegalStateException("twk_client_create failed");
        }

        receiveThread = new Thread(new Runnable() {
            @Override
            public void run() {
                receiveLoop();
            }
        }, "twk-receive");
        receiveThread.setDaemon(true);
        receiveThread.start();
    }

    /** Where unsolicited updates go. */
    public void setUpdateHandler(UpdateHandler handler) {
        this.updateHandler = handler;
    }

    /**
     * Invokes a core method. {@code params} is a positional argument array (see
     * docs/API.md). The handler runs on the receive thread.
     */
    public void send(String method, String params, ResultHandler handler) {
        if (method == null) {
            throw new IllegalArgumentException("method");
        }
        if (closed.get()) {
            if (handler != null) {
                handler.onResult(null, new WalletKitException("client is closed", null));
            }
            return;
        }

        // 0 is reserved for unsolicited updates, so ids start at 1.
        long requestId = nextRequestId.incrementAndGet();
        if (handler != null) {
            pending.put(requestId, handler);
        }

        try {
            Native.send(client, requestId, HostBridge.bytes(method),
                    HostBridge.bytes(params == null ? "[]" : params));
        } catch (Throwable failure) {
            pending.remove(requestId);
            throw failure;
        }
    }

    private void receiveLoop() {
        long[] requestId = new long[1];
        while (running) {
            byte[] message = Native.receive(client, 0.25, requestId);
            if (message == null) {
                continue; // timeout: loop so `running` is re-checked
            }

            String json = new String(message, HostBridge.UTF_8);
            if (requestId[0] == 0) {
                UpdateHandler handler = updateHandler;
                if (handler != null) {
                    try {
                        handler.onUpdate(json);
                    } catch (Throwable ignored) {
                        // A handler must not be able to kill the pump.
                    }
                }
                continue;
            }

            ResultHandler handler = pending.remove(requestId[0]);
            if (handler == null) {
                continue; // a cancelled or duplicate response
            }

            try {
                // Errors become exceptions here so callers never parse envelopes.
                if (json.startsWith("{\"error\"")) {
                    handler.onResult(null, new WalletKitException(WalletKitException.messageOf(json), json));
                } else {
                    handler.onResult(json, null);
                }
            } catch (Throwable ignored) {
                // As above: the pump survives a broken handler.
            }
        }
    }

    /**
     * Stops the pump, destroys the client and fails anything outstanding.
     *
     * <p>Order matters: the receive thread is inside twk_receive holding the
     * handle, so it is joined before the client is destroyed, and the delegate
     * block is freed last because a late completion may still reference it.
     */
    public void close() {
        if (!closed.compareAndSet(false, true)) {
            return;
        }

        running = false;
        try {
            receiveThread.join(5000);
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
        }

        Native.clientDestroy(client);
        Native.delegatesDestroy(delegates);

        for (Map.Entry<Long, ResultHandler> entry : pending.entrySet()) {
            entry.getValue().onResult(null, new WalletKitException("client is closed", null));
        }
        pending.clear();
    }
}
