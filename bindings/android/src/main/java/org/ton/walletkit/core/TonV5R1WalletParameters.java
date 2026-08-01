package org.ton.walletkit.core;

import java.util.List;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;

import org.ton.walletkit.api.TONNetwork;
import org.ton.walletkit.api.TONSignatureDomain;

/** Parameters for a V5R1 wallet. */
public final class TonV5R1WalletParameters extends TonV4R2WalletParameters {
    private TONSignatureDomain domain;

    public TonV5R1WalletParameters(TONNetwork network) {
        super(network);
    }

    public TonV5R1WalletParameters setDomain(TONSignatureDomain value) {
        this.domain = value;
        return this;
    }

    @Override
    public String toJson() {
        JsonObject params = json();
        if (domain != null) {
            params.add("domain", TonJson.GSON.toJsonTree(domain));
        }
        return params.toString();
    }
}
