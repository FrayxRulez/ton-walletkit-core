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
final class TonAdapterDescriptor {

    static final Parser<TonAdapterDescriptor> PARSER = new Parser<TonAdapterDescriptor>() {
        @Override
        public TonAdapterDescriptor parse(JSONObject json) {
            if (json == null) {
                return null;
            }
            TonAdapterDescriptor out = new TonAdapterDescriptor();
            out.adapterId = TonApiJson.optString(json, "adapterId");
            out.address = TonApiJson.optString(json, "address");
            out.publicKey = TonApiJson.optString(json, "publicKey");
            out.walletId = TonApiJson.optString(json, "walletId");
            out.network = TONNetwork.fromJson(TonApiJson.optObject(json, "network"));
            return out;
        }
    };

    String adapterId;
    String address;
    String publicKey;
    String walletId;
    TONNetwork network;
}
