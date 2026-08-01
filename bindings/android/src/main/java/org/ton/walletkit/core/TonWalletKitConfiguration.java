//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

package org.ton.walletkit.core;

import java.util.ArrayList;
import java.util.List;

import org.json.JSONArray;
import org.json.JSONObject;

import org.ton.walletkit.api.TonApiJson;

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
        JSONObject config = new JSONObject();

        JSONArray networks = new JSONArray();
        if (networkConfigurations != null) {
            for (TonNetworkConfiguration network : networkConfigurations) {
                networks.put(network.toJson());
            }
        }
        TonApiJson.put(config, "networks", networks);

        if (walletManifest != null) {
            TonApiJson.put(config, "walletManifest", walletManifest.toJson(platform, features));
            // walletkit reports the wallet to dapps through deviceInfo; kit-ios
            // derives it from the manifest rather than asking for it twice.
            JSONObject device = new JSONObject();
            TonApiJson.put(device, "platform", platform);
            TonApiJson.put(device, "appName", walletManifest.getAppName());
            TonApiJson.put(device, "appVersion", appVersion);
            TonApiJson.put(device, "maxProtocolVersion", Integer.valueOf(2));
            TonApiJson.put(device, "features", featuresJson());
            TonApiJson.put(config, "deviceInfo", device);
        }

        if (bridge != null) {
            TonApiJson.put(config, "bridge", bridge.toJson());
        }
        TonApiJson.put(config, "storagePrefix", storagePrefix);

        return config.toString();
    }

    private JSONArray featuresJson() {
        JSONArray supported = new JSONArray();
        if (features != null) {
            for (TonFeature feature : features) {
                if (feature != null) {
                    supported.put(feature.toJson());
                }
            }
        }
        return supported;
    }





}
