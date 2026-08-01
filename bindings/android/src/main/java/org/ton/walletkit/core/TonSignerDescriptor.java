//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

package org.ton.walletkit.core;

import org.json.JSONObject;

import org.ton.walletkit.api.Parser;
import org.ton.walletkit.api.TonApiJson;

/** A reply from the core, read field by field like the generated models. */
final class TonSignerDescriptor {

    static final Parser<TonSignerDescriptor> PARSER = new Parser<TonSignerDescriptor>() {
        @Override
        public TonSignerDescriptor parse(JSONObject json) {
            if (json == null) {
                return null;
            }
            TonSignerDescriptor out = new TonSignerDescriptor();
            out.signerId = TonApiJson.optString(json, "signerId");
            out.publicKey = TonApiJson.optString(json, "publicKey");
            return out;
        }
    };

    String signerId;
    String publicKey;
}
