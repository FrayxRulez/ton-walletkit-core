package org.ton.walletkit.core;

import java.util.ArrayList;
import java.util.List;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;

import org.ton.walletkit.api.TONNetwork;

/**
 * How the kit is built (kit-ios TONWalletKitConfiguration).
 *
 * <p>Written by hand rather than generated: the wire shape is walletkit's
 * TonWalletKitOptions, which is not always what kit-ios calls the field.
 */
public final class TonWalletKitConfiguration {

    private List<TonNetworkConfiguration> networkConfigurations = new ArrayList<TonNetworkConfiguration>();
    private TonWalletManifest walletManifest;
    private TonBridgeConfiguration bridge;
    private List<TonFeature> features = new ArrayList<TonFeature>();
    private String platform = "android";
    private String appVersion;
    private String storagePrefix;

    public List<TonNetworkConfiguration> getNetworkConfigurations() {
        return networkConfigurations;
    }

    public TonWalletKitConfiguration setNetworkConfigurations(List<TonNetworkConfiguration> value) {
        this.networkConfigurations = value;
        return this;
    }

    public TonWalletKitConfiguration setWalletManifest(TonWalletManifest value) {
        this.walletManifest = value;
        return this;
    }

    /** Required for TON Connect: approvals travel back through the relay. */
    public TonWalletKitConfiguration setBridge(TonBridgeConfiguration value) {
        this.bridge = value;
        return this;
    }

    public TonWalletKitConfiguration setFeatures(List<TonFeature> value) {
        this.features = value;
        return this;
    }

    public TonWalletKitConfiguration setPlatform(String value) {
        this.platform = value;
        return this;
    }

    public TonWalletKitConfiguration setAppVersion(String value) {
        this.appVersion = value;
        return this;
    }

    /** Namespaces one account's storage keys. */
    public TonWalletKitConfiguration setStoragePrefix(String value) {
        this.storagePrefix = value;
        return this;
    }

    /** The config object initWalletKit takes. */
    String toJson() {
        JsonObject config = new JsonObject();

        JsonArray networks = new JsonArray();
        if (networkConfigurations != null) {
            for (TonNetworkConfiguration network : networkConfigurations) {
                networks.add(network.toJson());
            }
        }
        config.add("networks", networks);

        if (walletManifest != null) {
            config.add("walletManifest", walletManifest.toJson(platform, features));
            // walletkit reports the wallet to dapps through deviceInfo; kit-ios
            // derives it from the manifest rather than asking for it twice.
            JsonObject device = new JsonObject();
            device.addProperty("platform", platform);
            device.addProperty("appName", walletManifest.getAppName());
            device.addProperty("appVersion", appVersion);
            device.addProperty("maxProtocolVersion", 2);
            device.add("features", TonJson.GSON.toJsonTree(features));
            config.add("deviceInfo", device);
        }

        if (bridge != null) {
            config.add("bridge", bridge.toJson());
        }
        if (storagePrefix != null) {
            config.addProperty("storagePrefix", storagePrefix);
        }

        return config.toString();
    }





}
