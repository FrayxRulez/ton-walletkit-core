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

/** The wallet's TON Connect manifest. */
public final class TonWalletManifest {
    private String name;
    private String appName;
    private String imageUrl;
    private String aboutUrl;
    private String universalLink;
    private String bridgeUrl;

    public String getAppName() {
        return appName;
    }

    public TonWalletManifest setName(String value) {
        this.name = value;
        return this;
    }

    public TonWalletManifest setAppName(String value) {
        this.appName = value;
        return this;
    }

    public TonWalletManifest setImageUrl(String value) {
        this.imageUrl = value;
        return this;
    }

    public TonWalletManifest setAboutUrl(String value) {
        this.aboutUrl = value;
        return this;
    }

    public TonWalletManifest setUniversalLink(String value) {
        this.universalLink = value;
        return this;
    }

    public TonWalletManifest setBridgeUrl(String value) {
        this.bridgeUrl = value;
        return this;
    }

    JSONObject toJson(String platform, List<TonFeature> features) {
        JSONObject manifest = new JSONObject();
        TonApiJson.put(manifest, "name", name);
        TonApiJson.put(manifest, "appName", appName);
        TonApiJson.put(manifest, "imageUrl", imageUrl);
        TonApiJson.put(manifest, "aboutUrl", aboutUrl);
        TonApiJson.put(manifest, "universalLink", universalLink);
        TonApiJson.put(manifest, "bridgeUrl", bridgeUrl);

        JSONArray platforms = new JSONArray();
        platforms.put(platform);
        TonApiJson.put(manifest, "platforms", platforms);

        if (features != null) {
            JSONArray supported = new JSONArray();
            for (TonFeature feature : features) {
                if (feature != null) {
                    supported.put(feature.toJson());
                }
            }
            TonApiJson.put(manifest, "features", supported);
        }
        return manifest;
    }
}
