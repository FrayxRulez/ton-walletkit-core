package org.ton.walletkit.core;

import java.util.List;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;

import org.ton.walletkit.api.TONNetwork;
import org.ton.walletkit.api.TONSignatureDomain;

/** Parameters for a V4R2 wallet. */
public class TonV4R2WalletParameters {
    final TONNetwork network;
    Integer walletId;
    Integer workchain;

    public TonV4R2WalletParameters(TONNetwork network) {
        this.network = network;
    }

    /** TON subwallet id, not the kit's walletId. */
    public TonV4R2WalletParameters setWalletId(Integer value) {
        this.walletId = value;
        return this;
    }

    public TonV4R2WalletParameters setWorkchain(Integer value) {
        this.workchain = value;
        return this;
    }

    JsonObject json() {
        JsonObject params = new JsonObject();
        if (network != null) {
            params.add("network", TonJson.GSON.toJsonTree(network));
        }
        if (walletId != null) {
            params.addProperty("walletId", walletId);
        }
        if (workchain != null) {
            params.addProperty("workchain", workchain);
        }
        return params;
    }

    /** Already-serialized, because the argument array takes it as raw JSON. */
    public String toJson() {
        return json().toString();
    }
}
