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
        JSONObject params = json();
        if (domain != null) {
            TonApiJson.put(params, "domain", domain.toJson());
        }
        return params.toString();
    }
}
