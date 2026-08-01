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

    JSONObject json() {
        JSONObject params = new JSONObject();
        if (network != null) {
            TonApiJson.put(params, "network", network.toJson());
        }
        if (walletId != null) {
            TonApiJson.put(params, "walletId", walletId);
        }
        if (workchain != null) {
            TonApiJson.put(params, "workchain", workchain);
        }
        return params;
    }

    /** Already-serialized, because the argument array takes it as raw JSON. */
    public String toJson() {
        return json().toString();
    }
}
