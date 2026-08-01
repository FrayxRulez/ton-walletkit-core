package org.ton.walletkit.core;

import java.util.List;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;

import org.ton.walletkit.api.TONNetwork;
import org.ton.walletkit.api.TONSignatureDomain;

/** TON Connect relay settings. */
public final class TonBridgeConfiguration {
    private String bridgeUrl;
    private String jsBridgeKey;

    public TonBridgeConfiguration setBridgeUrl(String value) {
        this.bridgeUrl = value;
        return this;
    }

    /**
     * Only names the key an in-app browser would inject under. Never sets
     * enableJsBridge: walletkit then demands a transport *function*, which a
     * JSON ABI cannot carry, and initialize fails outright.
     */
    public TonBridgeConfiguration setJsBridgeKey(String value) {
        this.jsBridgeKey = value;
        return this;
    }

    JsonObject toJson() {
        JsonObject config = new JsonObject();
        if (bridgeUrl != null) {
            config.addProperty("bridgeUrl", bridgeUrl);
        }
        if (jsBridgeKey != null) {
            config.addProperty("jsBridgeKey", jsBridgeKey);
        }
        return config;
    }
}
