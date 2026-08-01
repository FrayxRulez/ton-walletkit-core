//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// How the kit is built (kit-ios TONWalletKitConfiguration).
//
// Serialized by hand rather than by a serializer: these are our own types, and
// .NET Native cannot reflect over them. The wire shape is walletkit's
// TonWalletKitOptions, which is not always what kit-ios calls the field.
//
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Text.Json;
using Ton.WalletKit.Api.Model;

namespace Ton.WalletKit
{
    /// <summary>An API endpoint for one network (kit-ios APIClientConfiguration).</summary>
    public sealed class TonApiClientConfiguration
    {
        /// <summary>Base URL, e.g. https://testnet.toncenter.com. Null uses the default.</summary>
        public string Url { get; set; }

        /// <summary>API key, when the endpoint requires one.</summary>
        public string Key { get; set; }

        /// <summary>Request timeout in milliseconds.</summary>
        public int? TimeoutMs { get; set; }
    }

    /// <summary>One network the kit talks to (kit-ios NetworkConfiguration).</summary>
    public sealed class TonNetworkConfiguration
    {
        /// <summary>Chain id: "-3" testnet, "-239" mainnet.</summary>
        public string ChainId { get; set; }

        /// <summary>Endpoint for this network.</summary>
        public TonApiClientConfiguration ApiClientConfiguration { get; set; }

        /// <summary>Configures a network by chain id.</summary>
        public TonNetworkConfiguration(string chainId, TonApiClientConfiguration apiClientConfiguration = null)
        {
            ChainId = chainId;
            ApiClientConfiguration = apiClientConfiguration;
        }
    }

    /// <summary>The wallet's TON Connect manifest (kit-ios Manifest).</summary>
    public sealed class TonWalletManifest
    {
        /// <summary>Display name shown to dapps.</summary>
        public string Name { get; set; }

        /// <summary>Stable identifier, e.g. "unigram".</summary>
        public string AppName { get; set; }

        /// <summary>Icon URL.</summary>
        public string ImageUrl { get; set; }

        /// <summary>Information page URL.</summary>
        public string AboutUrl { get; set; }

        /// <summary>TON DNS name, if any.</summary>
        public string TonDns { get; set; }

        /// <summary>Universal link dapps use to reach the wallet.</summary>
        public string UniversalLink { get; set; }

        /// <summary>Custom-scheme link, if any.</summary>
        public string DeepLink { get; set; }

        /// <summary>TON Connect relay this wallet listens on.</summary>
        public string BridgeUrl { get; set; }

        /// <summary>Platforms the wallet runs on. Defaults to "windows".</summary>
        public IList<string> Platforms { get; set; }
    }

    /// <summary>TON Connect relay settings (kit-ios Bridge).</summary>
    public sealed class TonBridgeConfiguration
    {
        /// <summary>
        /// Relay URL. Required to answer dapps at all: approvals travel back
        /// through the relay, and without it approving fails with
        /// "Bridge not initialized for sending response".
        /// </summary>
        public string BridgeUrl { get; set; }

        /// <summary>
        /// Key an in-app browser would inject the JS bridge under. Serving a dapp
        /// inside a WebView is not wired up yet — that needs a transport function
        /// on the JS side — so this only names the key.
        /// </summary>
        public string JsBridgeKey { get; set; }

        /// <summary>Heartbeat interval, milliseconds.</summary>
        public int? HeartbeatIntervalMs { get; set; }

        /// <summary>Reconnect delay, milliseconds.</summary>
        public int? ReconnectIntervalMs { get; set; }

        /// <summary>How many times to retry a dropped relay connection.</summary>
        public int? MaxReconnectAttempts { get; set; }
    }

    /// <summary>Test hooks (kit-ios DevConfiguration).</summary>
    public sealed class TonDevConfiguration
    {
        /// <summary>Builds and signs transactions but never broadcasts them.</summary>
        public bool? DisableNetworkSend { get; set; }

        /// <summary>Accepts manifests whose domain does not match the dapp.</summary>
        public bool? DisableManifestDomainCheck { get; set; }
    }

    /// <summary>Everything the kit needs to start (kit-ios TONWalletKitConfiguration).</summary>
    public sealed class TonWalletKitConfiguration
    {
        /// <summary>Networks to talk to. Defaults to testnet when empty.</summary>
        public IList<TonNetworkConfiguration> NetworkConfigurations { get; set; }

        /// <summary>The wallet's manifest, shown to dapps on connect.</summary>
        public TonWalletManifest WalletManifest { get; set; }

        /// <summary>TON Connect relay settings. Required for TON Connect.</summary>
        public TonBridgeConfiguration Bridge { get; set; }

        /// <summary>TON Connect features this wallet supports.</summary>
        public IList<TonFeature> Features { get; set; }

        /// <summary>Test hooks; leave null in production.</summary>
        public TonDevConfiguration DevConfiguration { get; set; }

        /// <summary>Reported to dapps as the device platform.</summary>
        public string Platform { get; set; } = "windows";

        /// <summary>Reported to dapps as the app version.</summary>
        public string AppVersion { get; set; }

        /// <summary>
        /// Prefix for every host storage key, so several accounts can share one
        /// store without colliding.
        /// </summary>
        public string StoragePrefix { get; set; }

        /// <summary>Builds the config object initWalletKit takes.</summary>
        internal string ToJson()
        {
            using (var buffer = new MemoryStream())
            using (var writer = new Utf8JsonWriter(buffer))
            {
                writer.WriteStartObject();

                writer.WritePropertyName("networks");
                writer.WriteStartArray();
                if (NetworkConfigurations != null)
                {
                    foreach (var network in NetworkConfigurations)
                    {
                        WriteNetwork(writer, network);
                    }
                }
                writer.WriteEndArray();

                if (WalletManifest != null)
                {
                    writer.WritePropertyName("walletManifest");
                    WriteManifest(writer);

                    // walletkit reports the wallet to dapps through deviceInfo;
                    // kit-ios derives it from the manifest rather than asking for
                    // it twice, and so do we.
                    writer.WritePropertyName("deviceInfo");
                    WriteDeviceInfo(writer);
                }

                if (Bridge != null)
                {
                    writer.WritePropertyName("bridge");
                    WriteBridge(writer);
                }

                if (DevConfiguration != null)
                {
                    writer.WritePropertyName("dev");
                    writer.WriteStartObject();
                    if (DevConfiguration.DisableNetworkSend.HasValue)
                    {
                        writer.WriteBoolean("disableNetworkSend", DevConfiguration.DisableNetworkSend.Value);
                    }
                    if (DevConfiguration.DisableManifestDomainCheck.HasValue)
                    {
                        writer.WriteBoolean("disableManifestDomainCheck",
                                            DevConfiguration.DisableManifestDomainCheck.Value);
                    }
                    writer.WriteEndObject();
                }

                if (StoragePrefix != null)
                {
                    writer.WriteString("storagePrefix", StoragePrefix);
                }

                writer.WriteEndObject();
                writer.Flush();
                return Encoding.UTF8.GetString(buffer.ToArray());
            }
        }

        private static void WriteNetwork(Utf8JsonWriter writer, TonNetworkConfiguration network)
        {
            writer.WriteStartObject();
            writer.WriteString("chainId", network.ChainId);

            var client = network.ApiClientConfiguration;
            if (client != null)
            {
                if (client.Url != null)
                {
                    writer.WriteString("endpoint", client.Url);
                }
                if (client.Key != null)
                {
                    writer.WriteString("apiKey", client.Key);
                }
                if (client.TimeoutMs.HasValue)
                {
                    writer.WriteNumber("timeout", client.TimeoutMs.Value);
                }
            }
            writer.WriteEndObject();
        }

        private void WriteManifest(Utf8JsonWriter writer)
        {
            var manifest = WalletManifest;
            writer.WriteStartObject();
            WriteIfSet(writer, "name", manifest.Name);
            WriteIfSet(writer, "appName", manifest.AppName);
            WriteIfSet(writer, "imageUrl", manifest.ImageUrl);
            WriteIfSet(writer, "aboutUrl", manifest.AboutUrl);
            WriteIfSet(writer, "tondns", manifest.TonDns);
            WriteIfSet(writer, "universalLink", manifest.UniversalLink);
            WriteIfSet(writer, "deepLink", manifest.DeepLink);
            WriteIfSet(writer, "bridgeUrl", manifest.BridgeUrl);

            writer.WritePropertyName("platforms");
            writer.WriteStartArray();
            if (manifest.Platforms != null && manifest.Platforms.Count > 0)
            {
                foreach (string platform in manifest.Platforms)
                {
                    writer.WriteStringValue(platform);
                }
            }
            else
            {
                writer.WriteStringValue(Platform);
            }
            writer.WriteEndArray();

            WriteFeatures(writer);
            writer.WriteEndObject();
        }

        private void WriteDeviceInfo(Utf8JsonWriter writer)
        {
            writer.WriteStartObject();
            WriteIfSet(writer, "platform", Platform);
            WriteIfSet(writer, "appName", WalletManifest.AppName);
            WriteIfSet(writer, "appVersion", AppVersion);
            // 2 is the current TON Connect protocol version, as in kit-ios.
            writer.WriteNumber("maxProtocolVersion", 2);
            WriteFeatures(writer);
            writer.WriteEndObject();
        }

        private void WriteBridge(Utf8JsonWriter writer)
        {
            writer.WriteStartObject();
            WriteIfSet(writer, "bridgeUrl", Bridge.BridgeUrl);
            if (Bridge.JsBridgeKey != null)
            {
                // Only the injection key. Never enableJsBridge: that makes
                // walletkit demand a jsBridgeTransport *function*, which a JSON
                // ABI cannot carry, and initWalletKit then fails outright with
                // "JS Bridge transport is not configured". Serving an in-app
                // browser needs a transport in js/src/main.ts first.
                writer.WriteString("jsBridgeKey", Bridge.JsBridgeKey);
            }
            if (Bridge.HeartbeatIntervalMs.HasValue)
            {
                writer.WriteNumber("heartbeatInterval", Bridge.HeartbeatIntervalMs.Value);
            }
            if (Bridge.ReconnectIntervalMs.HasValue)
            {
                writer.WriteNumber("reconnectInterval", Bridge.ReconnectIntervalMs.Value);
            }
            if (Bridge.MaxReconnectAttempts.HasValue)
            {
                writer.WriteNumber("maxReconnectAttempts", Bridge.MaxReconnectAttempts.Value);
            }
            writer.WriteEndObject();
        }

        private void WriteFeatures(Utf8JsonWriter writer)
        {
            writer.WritePropertyName("features");
            writer.WriteStartArray();
            if (Features != null)
            {
                foreach (var feature in Features)
                {
                    JsonSerializer.Serialize(writer, feature, TonJson.Options);
                }
            }
            writer.WriteEndArray();
        }

        private static void WriteIfSet(Utf8JsonWriter writer, string name, string value)
        {
            if (value != null)
            {
                writer.WriteString(name, value);
            }
        }
    }

    /// <summary>Parameters for a V4R2 wallet (kit-ios TONV4R2WalletParameters).</summary>
    public class TonV4R2WalletParameters
    {
        /// <summary>The network this wallet lives on.</summary>
        public TONNetwork Network { get; set; }

        /// <summary>TON subwallet id. Not the kit's walletId.</summary>
        public int? WalletId { get; set; }

        /// <summary>Workchain, 0 (basechain) unless you know otherwise.</summary>
        public int? Workchain { get; set; }

        /// <summary>Parameters for a wallet on <paramref name="network"/>.</summary>
        public TonV4R2WalletParameters(TONNetwork network)
        {
            Network = network;
        }

        /// <summary>Writes the options object the adapter factory takes.</summary>
        internal virtual string ToJson()
        {
            using (var buffer = new MemoryStream())
            using (var writer = new Utf8JsonWriter(buffer))
            {
                writer.WriteStartObject();
                WriteProperties(writer);
                writer.WriteEndObject();
                writer.Flush();
                return Encoding.UTF8.GetString(buffer.ToArray());
            }
        }

        internal void WriteProperties(Utf8JsonWriter writer)
        {
            if (Network != null)
            {
                writer.WritePropertyName("network");
                JsonSerializer.Serialize(writer, Network, TonJson.Options);
            }
            if (WalletId.HasValue)
            {
                writer.WriteNumber("walletId", WalletId.Value);
            }
            if (Workchain.HasValue)
            {
                writer.WriteNumber("workchain", Workchain.Value);
            }
        }
    }

    /// <summary>Parameters for a V5R1 wallet (kit-ios TONV5R1WalletParameters).</summary>
    public sealed class TonV5R1WalletParameters : TonV4R2WalletParameters
    {
        /// <summary>Signature domain; null uses the wallet's own chain.</summary>
        public TONSignatureDomain Domain { get; set; }

        /// <summary>Parameters for a wallet on <paramref name="network"/>.</summary>
        public TonV5R1WalletParameters(TONNetwork network) : base(network)
        {
        }

        /// <inheritdoc/>
        internal override string ToJson()
        {
            using (var buffer = new MemoryStream())
            using (var writer = new Utf8JsonWriter(buffer))
            {
                writer.WriteStartObject();
                WriteProperties(writer);
                if (Domain != null)
                {
                    writer.WritePropertyName("domain");
                    JsonSerializer.Serialize(writer, Domain, TonJson.Options);
                }
                writer.WriteEndObject();
                writer.Flush();
                return Encoding.UTF8.GetString(buffer.ToArray());
            }
        }
    }
}
