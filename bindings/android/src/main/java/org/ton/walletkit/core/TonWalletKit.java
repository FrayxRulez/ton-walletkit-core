//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

package org.ton.walletkit.core;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import org.json.JSONObject;

import org.ton.walletkit.api.Parser;
import org.ton.walletkit.api.TonApiJson;
import org.ton.walletkit.api.TONSendTransactionResponse;
import org.ton.walletkit.api.TONTransactionRequest;

/**
 * The wallet kit: keys, wallets, and TON Connect.
 *
 * <p>Mirrors kit-ios method for method. Everything mechanical is generated from
 * api/facade.json into TonWalletKitGenerated; what stays here is the transport
 * and the handful of methods whose bodies are not mechanical, each marked
 * {@code custom} in the schema so the two halves cannot drift apart.
 */
public final class TonWalletKit extends TonWalletKitGenerated {

    /** Builds a facade object from the descriptor the core returned. */
    interface Factory<D, T> {
        T create(TonWalletKit kit, D descriptor);
    }

    private final WalletKitClient client;
    private final TonWalletKitConfiguration configuration;

    private volatile boolean initialized;

    public TonWalletKit(WalletKitHost host, TonWalletKitConfiguration configuration) {
        if (configuration == null) {
            throw new IllegalArgumentException("configuration");
        }
        this.configuration = configuration;
        this.client = new WalletKitClient(host);
    }

    /** True once {@link #initialize} has completed. */
    public boolean isInitialized() {
        return initialized;
    }

    public TonWalletKitConfiguration getConfiguration() {
        return configuration;
    }

    /** Unsolicited updates: TON Connect requests, disconnects. */
    public void setUpdateHandler(WalletKitClient.UpdateHandler handler) {
        client.setUpdateHandler(handler);
    }

    /** Tears the kit down; the client joins its receive thread first. */
    public void close() {
        client.close();
    }

    // ---- custom methods (declared in api/facade.json) -----------------------

    /** Builds the kit inside the core. */
    public void initialize(final Callback<Void> callback) {
        client.send("initWalletKit", TonJson.argsRaw(configuration.toJson()),
                new WalletKitClient.ResultHandler() {
                    @Override
                    public void onResult(String result, WalletKitException error) {
                        if (error == null) {
                            initialized = true;
                        }
                        deliver(callback, null, error);
                    }
                });
    }

    /**
     * Generates a mnemonic and derives a V5R1 wallet from it in one step
     * (kit-ios createWallet). The wallet is not registered yet — pass the
     * adapter to {@link #add}.
     */
    public void createWallet(final TonV5R1WalletParameters parameters,
                             final Callback<TonWalletCreationResult> callback) {
        generateMnemonic(new Callback<TonMnemonic>() {
            @Override
            public void onResult(final TonMnemonic mnemonic, WalletKitException error) {
                if (error != null) {
                    deliver(callback, null, error);
                    return;
                }
                signer(mnemonic, new Callback<TonSigner>() {
                    @Override
                    public void onResult(TonSigner signer, WalletKitException error) {
                        if (error != null) {
                            deliver(callback, null, error);
                            return;
                        }
                        walletV5R1Adapter(signer, parameters, new Callback<TonWalletAdapter>() {
                            @Override
                            public void onResult(TonWalletAdapter adapter, WalletKitException error) {
                                deliver(callback, error == null
                                        ? new TonWalletCreationResult(mnemonic, adapter) : null, error);
                            }
                        });
                    }
                });
            }
        });
    }

    /** Signs and broadcasts a transaction from a wallet. */
    public void send(TONTransactionRequest transaction, TonWallet wallet,
                     Callback<TONSendTransactionResponse> callback) {
        if (wallet == null) {
            throw new IllegalArgumentException("wallet");
        }
        wallet.send(transaction, callback);
    }

    /** Balance of any address, in nanotons. */
    public void addressBalance(String address, String chainId, final Callback<TonAmount> callback) {
        client.send("getAddressBalance", TonJson.args(address, chainId(chainId)),
                new WalletKitClient.ResultHandler() {
                    @Override
                    public void onResult(String result, WalletKitException error) {
                        if (error != null) {
                            deliver(callback, null, error);
                            return;
                        }
                        // {address, balance} is a shape of the core's own.
                        String balance = TonApiJson.optString(TonJson.resultObject(result), "balance");
                        deliver(callback, TonAmount.fromNanotons(balance), null);
                    }
                });
    }

    /**
     * Handles a tc:// link. The dapp's request arrives as an update, not as the
     * result of this call.
     */
    public void connect(String url, Callback<Void> callback) {
        if (url != null && !url.contains("://")) {
            url = "tc://" + url.replaceAll("^/+", "");
        }
        invokeVoid("handleTonConnectUrl", TonJson.args(url), callback);
    }

    // ---- the invoker the generated code calls ------------------------------

    @Override
    String chainId(String chainId) {
        if (chainId != null) {
            return chainId;
        }
        List<TonNetworkConfiguration> networks = configuration.getNetworkConfigurations();
        return networks == null || networks.isEmpty() ? null : networks.get(0).getChainId();
    }

    @Override
    String handleOf(Object object) {
        if (object instanceof TonSignerImpl) {
            return ((TonSignerImpl) object).handle;
        }
        if (object instanceof TonWalletAdapterImpl) {
            return ((TonWalletAdapterImpl) object).handle;
        }
        throw new IllegalArgumentException("the object must come from this kit");
    }

    @Override
    @SuppressWarnings("unchecked")
    <T> void invoke(String method, String args, final Parser<T> parser, final Callback<?> callback) {
        final Callback<Object> typed = (Callback<Object>) callback;
        client.send(method, args, new WalletKitClient.ResultHandler() {
            @Override
            public void onResult(String result, WalletKitException error) {
                if (error != null) {
                    deliver(typed, null, error);
                    return;
                }
                // A method whose result is a bare value (a string, a number) has
                // no object to parse; the parser only applies to models.
                JSONObject value = TonJson.resultObject(result);
                deliver(typed, value == null ? TonJson.result(result) : parser.parse(value), null);
            }
        });
    }

    @Override
    void invokeVoid(String method, String args, final Callback<Void> callback) {
        client.send(method, args, new WalletKitClient.ResultHandler() {
            @Override
            public void onResult(String result, WalletKitException error) {
                deliver(callback, null, error);
            }
        });
    }

    @Override
    void invokeAmount(String method, String args, final Callback<TonAmount> callback) {
        client.send(method, args, new WalletKitClient.ResultHandler() {
            @Override
            public void onResult(String result, WalletKitException error) {
                if (error != null) {
                    deliver(callback, null, error);
                    return;
                }
                deliver(callback, TonAmount.fromNanotons(TonJson.resultString(result)), null);
            }
        });
    }

    @Override
    void invokeMnemonic(String method, String args, final Callback<TonMnemonic> callback) {
        client.send(method, args, new WalletKitClient.ResultHandler() {
            @Override
            public void onResult(String result, WalletKitException error) {
                if (error != null) {
                    deliver(callback, null, error);
                    return;
                }
                List<String> words = TonApiJson.stringList(TonJson.resultArray(result));
                deliver(callback, new TonMnemonic(words == null ? new ArrayList<String>() : words), null);
            }
        });
    }

    @Override
    void invokeString(String method, String args, final Callback<String> callback) {
        client.send(method, args, new WalletKitClient.ResultHandler() {
            @Override
            public void onResult(String result, WalletKitException error) {
                deliver(callback, error == null ? TonJson.resultString(result) : null, error);
            }
        });
    }

    @Override
    @SuppressWarnings("unchecked")
    <T> void invokeList(String method, String args, final Parser<T> parser, final Callback<?> callback) {
        final Callback<Object> typed = (Callback<Object>) callback;
        client.send(method, args, new WalletKitClient.ResultHandler() {
            @Override
            public void onResult(String result, WalletKitException error) {
                if (error != null) {
                    deliver(typed, null, error);
                    return;
                }
                List<T> items = TonApiJson.list(TonJson.resultArray(result), parser);
                deliver(typed, items == null ? new ArrayList<T>() : items, null);
            }
        });
    }

    @Override
    <D, T> void invokeObject(String method, String args, final Parser<D> descriptor,
                             final Factory<D, T> factory, final Callback<T> callback) {
        client.send(method, args, new WalletKitClient.ResultHandler() {
            @Override
            public void onResult(String result, WalletKitException error) {
                if (error != null) {
                    deliver(callback, null, error);
                    return;
                }
                D value = descriptor.parse(TonJson.resultObject(result));
                deliver(callback, value == null ? null : factory.create(TonWalletKit.this, value), null);
            }
        });
    }

    @Override
    <D, T> void invokeObjectList(String method, String args, final Parser<D> descriptor,
                                 final Factory<D, T> factory, final Callback<List<T>> callback) {
        client.send(method, args, new WalletKitClient.ResultHandler() {
            @Override
            public void onResult(String result, WalletKitException error) {
                if (error != null) {
                    deliver(callback, null, error);
                    return;
                }
                List<D> values = TonApiJson.list(TonJson.resultArray(result), descriptor);
                List<T> items = new ArrayList<T>();
                if (values != null) {
                    for (D value : values) {
                        if (value != null) {
                            items.add(factory.create(TonWalletKit.this, value));
                        }
                    }
                }
                deliver(callback, items, null);
            }
        });
    }

    /** A callback must never be able to kill the receive thread. */
    private static <T> void deliver(Callback<T> callback, T result, WalletKitException error) {
        if (callback == null) {
            return;
        }
        try {
            callback.onResult(result, error);
        } catch (RuntimeException ignored) {
            // The pump survives a broken callback.
        }
    }
}
