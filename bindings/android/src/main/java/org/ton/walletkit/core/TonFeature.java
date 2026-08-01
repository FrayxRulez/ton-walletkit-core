//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

package org.ton.walletkit.core;

import java.util.List;

import org.json.JSONObject;

import org.ton.walletkit.api.Parser;
import org.ton.walletkit.api.TonApiJson;

/** A TON Connect feature this wallet supports. */
public final class TonFeature {

    static final Parser<TonFeature> PARSER = new Parser<TonFeature>() {
        @Override
        public TonFeature parse(JSONObject json) {
            if (json == null) {
                return null;
            }
            TonFeature out = new TonFeature();
            out.name = TonApiJson.optString(json, "name");
            Double maxMessages = TonApiJson.optDouble(json, "maxMessages");
            out.maxMessages = maxMessages == null ? null : Integer.valueOf(maxMessages.intValue());
            out.extraCurrencySupported = TonApiJson.optBoolean(json, "extraCurrencySupported");
            out.types = TonApiJson.stringList(TonApiJson.optArray(json, "types"));
            return out;
        }
    };

    public String name;

    /** SendTransaction: how many messages one transaction may carry. */
    public Integer maxMessages;

    /** SendTransaction: whether extra currencies may be attached. */
    public Boolean extraCurrencySupported;

    /** SignData: the payload types accepted. */
    public List<String> types;

    public TonFeature() {
    }

    public TonFeature(String name) {
        this.name = name;
    }

    /** How the feature travels to walletkit, inside the wallet manifest. */
    JSONObject toJson() {
        JSONObject json = new JSONObject();
        TonApiJson.put(json, "name", name);
        TonApiJson.put(json, "maxMessages", maxMessages);
        TonApiJson.put(json, "extraCurrencySupported", extraCurrencySupported);
        TonApiJson.put(json, "types", TonApiJson.array(types));
        return json;
    }
}
