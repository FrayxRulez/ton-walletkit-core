package org.ton.walletkit.core;

import java.util.List;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;

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

    JsonObject toJson(String platform, List<TonFeature> features) {
        JsonObject manifest = new JsonObject();
        manifest.addProperty("name", name);
        manifest.addProperty("appName", appName);
        manifest.addProperty("imageUrl", imageUrl);
        manifest.addProperty("aboutUrl", aboutUrl);
        manifest.addProperty("universalLink", universalLink);
        manifest.addProperty("bridgeUrl", bridgeUrl);

        JsonArray platforms = new JsonArray();
        platforms.add(platform);
        manifest.add("platforms", platforms);
        manifest.add("features", TonJson.GSON.toJsonTree(features));
        return manifest;
    }
}
