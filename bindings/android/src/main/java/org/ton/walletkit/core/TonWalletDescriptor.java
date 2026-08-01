//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

package org.ton.walletkit.core;

import org.json.JSONObject;

import org.ton.walletkit.api.Parser;
import org.ton.walletkit.api.TONNetwork;
import org.ton.walletkit.api.TonApiJson;

/** A reply from the core, read field by field like the generated models. */
final class TonWalletDescriptor {

    static final Parser<TonWalletDescriptor> PARSER = new Parser<TonWalletDescriptor>() {
        @Override
        public TonWalletDescriptor parse(JSONObject json) {
            if (json == null) {
                return null;
            }
            TonWalletDescriptor out = new TonWalletDescriptor();
            out.walletId = TonApiJson.optString(json, "walletId");
            out.address = TonApiJson.optString(json, "address");
            out.publicKey = TonApiJson.optString(json, "publicKey");
            out.version = TonApiJson.optString(json, "version");
            out.network = TONNetwork.fromJson(TonApiJson.optObject(json, "network"));
            return out;
        }
    };

    String walletId;
    String address;
    String publicKey;
    String version;
    TONNetwork network;
}
