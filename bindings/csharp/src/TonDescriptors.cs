//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// The core's own reply shapes, and their converters.
//
// These are not walletkit models — they are what the JS layer returns when it
// hands back a reference (an id plus the fields kit-ios exposes as synchronous
// properties). Hand-written for the same reason the generated models are:
// .NET Native cannot reflect over them.
//
using System;
using System.Collections.Generic;
using System.Text.Json;
using System.Text.Json.Serialization;
using Ton.WalletKit.Api.Model;

namespace Ton.WalletKit
{
    /// <summary>Wallet contract versions (kit-ios TONWalletVersion).</summary>
    public enum TonWalletVersion
    {
        /// <summary>Unrecognised or absent.</summary>
        Unknown = 0,

        /// <summary>Wallet v4 revision 2.</summary>
        V4R2,

        /// <summary>Wallet v5 revision 1, the current default.</summary>
        V5R1,
    }

    /// <summary>
    /// A TON Connect feature a wallet supports, as advertised in the manifest and
    /// reported by <see cref="ITonWalletAdapter.SupportedFeaturesAsync"/>.
    /// </summary>
    public sealed class TonFeature
    {
        /// <summary>"SendTransaction", "SignData", …</summary>
        public string Name { get; set; }

        /// <summary>SendTransaction: how many messages one transaction may carry.</summary>
        public int? MaxMessages { get; set; }

        /// <summary>SendTransaction: whether extra currencies may be attached.</summary>
        public bool? ExtraCurrencySupported { get; set; }

        /// <summary>SignData: the payload types accepted ("text", "binary", "cell").</summary>
        public IList<string> Types { get; set; }
    }

    /// <summary>A signer created in the core; the secret never crosses the ABI.</summary>
    internal sealed class TonSignerDescriptor
    {
        public string SignerId { get; set; }

        public string PublicKey { get; set; }
    }

    /// <summary>A wallet adapter created in the core.</summary>
    internal sealed class TonAdapterDescriptor
    {
        public string AdapterId { get; set; }

        public string Address { get; set; }

        public string PublicKey { get; set; }

        public string WalletId { get; set; }

        public TONNetwork Network { get; set; }
    }

    /// <summary>The reply to a watch-only balance lookup.</summary>
    internal sealed class TonAddressBalance
    {
        public string Address { get; set; }

        public string Balance { get; set; }
    }

    /// <summary>A wallet registered with the kit.</summary>
    internal sealed class TonWalletDescriptor
    {
        public string WalletId { get; set; }

        public string Address { get; set; }

        public string PublicKey { get; set; }

        public TonWalletVersion Version { get; set; }

        public TONNetwork Network { get; set; }
    }

    /// <summary>
    /// Shared reading for the descriptors: walks an object's properties, leaving
    /// the reader on the closing brace. Written as a loop rather than an iterator
    /// because Utf8JsonReader is a ref struct and cannot escape into one.
    /// </summary>
    internal static class TonReader
    {
        /// <summary>
        /// Advances to the next property of the object the reader is positioned on
        /// and to its value. Returns false at the end of the object.
        /// </summary>
        public static bool NextProperty(ref Utf8JsonReader reader, int depth, out string name)
        {
            while (reader.Read())
            {
                if (reader.TokenType == JsonTokenType.EndObject && reader.CurrentDepth == depth)
                {
                    break;
                }

                if (reader.TokenType == JsonTokenType.PropertyName && reader.CurrentDepth == depth + 1)
                {
                    name = reader.GetString();
                    reader.Read(); // move onto the value
                    return true;
                }
            }

            name = null;
            return false;
        }

        /// <summary>The current value as a string, or null.</summary>
        public static string String(ref Utf8JsonReader reader)
        {
            return reader.TokenType == JsonTokenType.String ? reader.GetString() : null;
        }

        /// <summary>The current value as an int, or null.</summary>
        public static int? Int(ref Utf8JsonReader reader)
        {
            return reader.TokenType == JsonTokenType.Number ? reader.GetInt32() : (int?)null;
        }

        /// <summary>The current value as a bool, or null.</summary>
        public static bool? Bool(ref Utf8JsonReader reader)
        {
            if (reader.TokenType == JsonTokenType.True)
            {
                return true;
            }
            return reader.TokenType == JsonTokenType.False ? false : (bool?)null;
        }
    }

    /// <summary>Reads <c>{signerId, publicKey}</c>.</summary>
    internal sealed class TonSignerDescriptorConverter : JsonConverter<TonSignerDescriptor>
    {
        /// <inheritdoc/>
        public override TonSignerDescriptor Read(ref Utf8JsonReader reader, Type typeToConvert,
                                                 JsonSerializerOptions options)
        {
            if (reader.TokenType != JsonTokenType.StartObject)
            {
                return null;
            }

            int depth = reader.CurrentDepth;
            var value = new TonSignerDescriptor();
            string name;
            while (TonReader.NextProperty(ref reader, depth, out name))
            {
                switch (name)
                {
                    case "signerId": value.SignerId = TonReader.String(ref reader); break;
                    case "publicKey": value.PublicKey = TonReader.String(ref reader); break;
                    default: reader.Skip(); break;
                }
            }
            return value;
        }

        /// <inheritdoc/>
        public override void Write(Utf8JsonWriter writer, TonSignerDescriptor value, JsonSerializerOptions options)
        {
            writer.WriteStartObject();
            writer.WriteString("signerId", value.SignerId);
            writer.WriteString("publicKey", value.PublicKey);
            writer.WriteEndObject();
        }
    }

    /// <summary>Reads the adapter descriptor.</summary>
    internal sealed class TonAdapterDescriptorConverter : JsonConverter<TonAdapterDescriptor>
    {
        /// <inheritdoc/>
        public override TonAdapterDescriptor Read(ref Utf8JsonReader reader, Type typeToConvert,
                                                  JsonSerializerOptions options)
        {
            if (reader.TokenType != JsonTokenType.StartObject)
            {
                return null;
            }

            int depth = reader.CurrentDepth;
            var value = new TonAdapterDescriptor();
            string name;
            while (TonReader.NextProperty(ref reader, depth, out name))
            {
                switch (name)
                {
                    case "adapterId": value.AdapterId = TonReader.String(ref reader); break;
                    case "address": value.Address = TonReader.String(ref reader); break;
                    case "publicKey": value.PublicKey = TonReader.String(ref reader); break;
                    case "walletId": value.WalletId = TonReader.String(ref reader); break;
                    case "network":
                        value.Network = reader.TokenType == JsonTokenType.StartObject
                            ? JsonSerializer.Deserialize<TONNetwork>(ref reader, options)
                            : null;
                        break;
                    default: reader.Skip(); break;
                }
            }
            return value;
        }

        /// <inheritdoc/>
        public override void Write(Utf8JsonWriter writer, TonAdapterDescriptor value, JsonSerializerOptions options)
        {
            writer.WriteStartObject();
            writer.WriteString("adapterId", value.AdapterId);
            writer.WriteString("address", value.Address);
            writer.WriteString("publicKey", value.PublicKey);
            writer.WriteString("walletId", value.WalletId);
            writer.WriteEndObject();
        }
    }

    /// <summary>Reads <c>{address, balance}</c>.</summary>
    internal sealed class TonAddressBalanceConverter : JsonConverter<TonAddressBalance>
    {
        /// <inheritdoc/>
        public override TonAddressBalance Read(ref Utf8JsonReader reader, Type typeToConvert,
                                               JsonSerializerOptions options)
        {
            if (reader.TokenType != JsonTokenType.StartObject)
            {
                return null;
            }

            int depth = reader.CurrentDepth;
            var value = new TonAddressBalance();
            string name;
            while (TonReader.NextProperty(ref reader, depth, out name))
            {
                switch (name)
                {
                    case "address": value.Address = TonReader.String(ref reader); break;
                    case "balance": value.Balance = TonReader.String(ref reader); break;
                    default: reader.Skip(); break;
                }
            }
            return value;
        }

        /// <inheritdoc/>
        public override void Write(Utf8JsonWriter writer, TonAddressBalance value, JsonSerializerOptions options)
        {
            writer.WriteStartObject();
            writer.WriteString("address", value.Address);
            writer.WriteString("balance", value.Balance);
            writer.WriteEndObject();
        }
    }

    /// <summary>Reads the wallet descriptor.</summary>
    internal sealed class TonWalletDescriptorConverter : JsonConverter<TonWalletDescriptor>
    {
        /// <inheritdoc/>
        public override TonWalletDescriptor Read(ref Utf8JsonReader reader, Type typeToConvert,
                                                 JsonSerializerOptions options)
        {
            if (reader.TokenType != JsonTokenType.StartObject)
            {
                return null;
            }

            int depth = reader.CurrentDepth;
            var value = new TonWalletDescriptor();
            string name;
            while (TonReader.NextProperty(ref reader, depth, out name))
            {
                switch (name)
                {
                    case "walletId": value.WalletId = TonReader.String(ref reader); break;
                    case "address": value.Address = TonReader.String(ref reader); break;
                    case "publicKey": value.PublicKey = TonReader.String(ref reader); break;
                    case "version": value.Version = ParseVersion(TonReader.String(ref reader)); break;
                    case "network":
                        value.Network = reader.TokenType == JsonTokenType.StartObject
                            ? JsonSerializer.Deserialize<TONNetwork>(ref reader, options)
                            : null;
                        break;
                    default: reader.Skip(); break;
                }
            }
            return value;
        }

        private static TonWalletVersion ParseVersion(string value)
        {
            if (string.Equals(value, "v5r1", StringComparison.OrdinalIgnoreCase))
            {
                return TonWalletVersion.V5R1;
            }
            if (string.Equals(value, "v4r2", StringComparison.OrdinalIgnoreCase))
            {
                return TonWalletVersion.V4R2;
            }
            return TonWalletVersion.Unknown;
        }

        /// <inheritdoc/>
        public override void Write(Utf8JsonWriter writer, TonWalletDescriptor value, JsonSerializerOptions options)
        {
            writer.WriteStartObject();
            writer.WriteString("walletId", value.WalletId);
            writer.WriteString("address", value.Address);
            writer.WriteString("publicKey", value.PublicKey);
            writer.WriteEndObject();
        }
    }

    /// <summary>
    /// Bytes as the ABI carries them: an array of numbers.
    ///
    /// Without this System.Text.Json would write base64, which the JS side would
    /// hand to Uint8Array.from and silently turn into nonsense — a wrong signing
    /// key rather than an error.
    /// </summary>
    internal sealed class TonBytesConverter : JsonConverter<byte[]>
    {
        /// <inheritdoc/>
        public override byte[] Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            if (reader.TokenType == JsonTokenType.String)
            {
                return Convert.FromBase64String(reader.GetString());
            }

            if (reader.TokenType != JsonTokenType.StartArray)
            {
                return null;
            }

            var bytes = new List<byte>();
            while (reader.Read() && reader.TokenType != JsonTokenType.EndArray)
            {
                bytes.Add(reader.GetByte());
            }
            return bytes.ToArray();
        }

        /// <inheritdoc/>
        public override void Write(Utf8JsonWriter writer, byte[] value, JsonSerializerOptions options)
        {
            writer.WriteStartArray();
            foreach (byte b in value)
            {
                writer.WriteNumberValue(b);
            }
            writer.WriteEndArray();
        }
    }

    /// <summary>Reads and writes a TON Connect feature.</summary>
    internal sealed class TonFeatureConverter : JsonConverter<TonFeature>
    {
        /// <inheritdoc/>
        public override TonFeature Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            // Protocol v1 advertised features as bare strings ("SendTransaction").
            if (reader.TokenType == JsonTokenType.String)
            {
                return new TonFeature { Name = reader.GetString() };
            }

            if (reader.TokenType != JsonTokenType.StartObject)
            {
                return null;
            }

            int depth = reader.CurrentDepth;
            var value = new TonFeature();
            string name;
            while (TonReader.NextProperty(ref reader, depth, out name))
            {
                switch (name)
                {
                    case "name": value.Name = TonReader.String(ref reader); break;
                    case "maxMessages": value.MaxMessages = TonReader.Int(ref reader); break;
                    case "extraCurrencySupported": value.ExtraCurrencySupported = TonReader.Bool(ref reader); break;
                    case "types":
                        value.Types = reader.TokenType == JsonTokenType.StartArray
                            ? JsonSerializer.Deserialize<List<string>>(ref reader, options)
                            : null;
                        break;
                    default: reader.Skip(); break;
                }
            }
            return value;
        }

        /// <inheritdoc/>
        public override void Write(Utf8JsonWriter writer, TonFeature value, JsonSerializerOptions options)
        {
            writer.WriteStartObject();
            writer.WriteString("name", value.Name);
            if (value.MaxMessages.HasValue)
            {
                writer.WriteNumber("maxMessages", value.MaxMessages.Value);
            }
            if (value.ExtraCurrencySupported.HasValue)
            {
                writer.WriteBoolean("extraCurrencySupported", value.ExtraCurrencySupported.Value);
            }
            if (value.Types != null)
            {
                writer.WritePropertyName("types");
                writer.WriteStartArray();
                foreach (string type in value.Types)
                {
                    writer.WriteStringValue(type);
                }
                writer.WriteEndArray();
            }
            writer.WriteEndObject();
        }
    }
}
