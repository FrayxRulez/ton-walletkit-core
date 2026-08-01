//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

package org.ton.walletkit.core;

import java.util.List;

import org.json.JSONArray;
import org.json.JSONObject;

import org.ton.walletkit.api.TonApiJson;

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

    JSONObject toJson() {
        JSONObject config = new JSONObject();
        if (bridgeUrl != null) {
            TonApiJson.put(config, "bridgeUrl", bridgeUrl);
        }
        if (jsBridgeKey != null) {
            TonApiJson.put(config, "jsBridgeKey", jsBridgeKey);
        }
        return config;
    }
}
