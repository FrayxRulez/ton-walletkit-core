//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

package org.ton.walletkit.core;

import java.util.List;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;

import org.ton.walletkit.api.TONNetwork;
import org.ton.walletkit.api.TONSignatureDomain;

/** One network the kit talks to. */
public final class TonNetworkConfiguration {
    private final String chainId;
    private String url;
    private String key;
    private Integer timeoutMs;

    public TonNetworkConfiguration(String chainId) {
        this.chainId = chainId;
    }

    public String getChainId() {
        return chainId;
    }

    public TonNetworkConfiguration setUrl(String value) {
        this.url = value;
        return this;
    }

    public TonNetworkConfiguration setKey(String value) {
        this.key = value;
        return this;
    }

    public TonNetworkConfiguration setTimeoutMs(Integer value) {
        this.timeoutMs = value;
        return this;
    }

    JsonObject toJson() {
        JsonObject network = new JsonObject();
        network.addProperty("chainId", chainId);
        if (url != null) {
            network.addProperty("endpoint", url);
        }
        if (key != null) {
            network.addProperty("apiKey", key);
        }
        if (timeoutMs != null) {
            network.addProperty("timeout", timeoutMs);
        }
        return network;
    }
}
