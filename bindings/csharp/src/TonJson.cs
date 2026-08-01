//
// JSON for the ABI: builds the positional argument arrays twk_send takes, and
// reads result envelopes into the generated DTOs.
//
// Everything goes through hand-written converters (GeneratedConverters, plus the
// few descriptors below), never reflection: the consumer ships on .NET Native,
// where reflection-based serialization is not available.
//
using System;
using System.IO;
using System.Text;
using System.Text.Json;
using Ton.WalletKit.Api.Client;

namespace Ton.WalletKit
{
    /// <summary>Serialization for the wallet ABI.</summary>
    internal static class TonJson
    {
        /// <summary>Options carrying every generated converter.</summary>
        public static readonly JsonSerializerOptions Options = CreateOptions();

        private static JsonSerializerOptions CreateOptions()
        {
            var options = new JsonSerializerOptions();
            GeneratedConverters.AddTo(options);

            // The core's own reply shapes (ids and descriptors), which are ours
            // rather than walletkit's, so they are not in the generated set.
            options.Converters.Add(new TonSignerDescriptorConverter());
            options.Converters.Add(new TonAdapterDescriptorConverter());
            options.Converters.Add(new TonWalletDescriptorConverter());
            options.Converters.Add(new TonAddressBalanceConverter());
            options.Converters.Add(new TonFeatureConverter());
            options.Converters.Add(new TonBytesConverter());
            return options;
        }

        /// <summary>An empty argument list.</summary>
        public static string Args()
        {
            return "[]";
        }

        /// <summary>A one-argument list.</summary>
        public static string Args<T1>(T1 first)
        {
            using (var buffer = new MemoryStream())
            using (var writer = new Utf8JsonWriter(buffer))
            {
                writer.WriteStartArray();
                JsonSerializer.Serialize(writer, first, Options);
                writer.WriteEndArray();
                writer.Flush();
                return Encoding.UTF8.GetString(buffer.ToArray());
            }
        }

        /// <summary>A two-argument list.</summary>
        public static string Args<T1, T2>(T1 first, T2 second)
        {
            using (var buffer = new MemoryStream())
            using (var writer = new Utf8JsonWriter(buffer))
            {
                writer.WriteStartArray();
                JsonSerializer.Serialize(writer, first, Options);
                JsonSerializer.Serialize(writer, second, Options);
                writer.WriteEndArray();
                writer.Flush();
                return Encoding.UTF8.GetString(buffer.ToArray());
            }
        }

        /// <summary>A three-argument list.</summary>
        public static string Args<T1, T2, T3>(T1 first, T2 second, T3 third)
        {
            using (var buffer = new MemoryStream())
            using (var writer = new Utf8JsonWriter(buffer))
            {
                writer.WriteStartArray();
                JsonSerializer.Serialize(writer, first, Options);
                JsonSerializer.Serialize(writer, second, Options);
                JsonSerializer.Serialize(writer, third, Options);
                writer.WriteEndArray();
                writer.Flush();
                return Encoding.UTF8.GetString(buffer.ToArray());
            }
        }

        /// <summary>A four-argument list.</summary>
        public static string Args<T1, T2, T3, T4>(T1 first, T2 second, T3 third, T4 fourth)
        {
            using (var buffer = new MemoryStream())
            using (var writer = new Utf8JsonWriter(buffer))
            {
                writer.WriteStartArray();
                JsonSerializer.Serialize(writer, first, Options);
                JsonSerializer.Serialize(writer, second, Options);
                JsonSerializer.Serialize(writer, third, Options);
                JsonSerializer.Serialize(writer, fourth, Options);
                writer.WriteEndArray();
                writer.Flush();
                return Encoding.UTF8.GetString(buffer.ToArray());
            }
        }

        /// <summary>
        /// An argument list from values that are already JSON. Used for the few
        /// arguments that write themselves (the configuration, wallet parameters).
        /// </summary>
        public static string ArgsRaw(params string[] jsonValues)
        {
            return "[" + string.Join(",", jsonValues) + "]";
        }

        /// <summary>Serializes a value on its own (used for nested payloads).</summary>
        public static string Serialize<T>(T value)
        {
            return JsonSerializer.Serialize(value, Options);
        }

        /// <summary>Deserializes a standalone document.</summary>
        public static T Deserialize<T>(string json)
        {
            return json == null ? default(T) : JsonSerializer.Deserialize<T>(json, Options);
        }

        /// <summary>
        /// Reads the value of the envelope's <c>result</c> member. Errors never
        /// reach here — the transport turns them into exceptions — so anything
        /// without a result is a shape the core should not have produced, and
        /// yields the default.
        /// </summary>
        public static T Result<T>(string envelope)
        {
            return Member<T>(envelope, "result");
        }

        /// <summary>
        /// The raw JSON of the envelope's result, or false when there is none
        /// (a method that answers with nothing, or a null result).
        /// </summary>
        public static bool TryResultJson(string envelope, out string json)
        {
            json = null;
            if (envelope == null)
            {
                return false;
            }

            using (var document = JsonDocument.Parse(envelope))
            {
                JsonElement result;
                if (!document.RootElement.TryGetProperty("result", out result) ||
                    result.ValueKind == JsonValueKind.Null ||
                    result.ValueKind == JsonValueKind.Undefined)
                {
                    return false;
                }

                json = result.GetRawText();
                return true;
            }
        }

        /// <summary>
        /// Splits an <c>{"event":{"type":…,"payload":…}}</c> envelope. Returns
        /// false for anything else, including request results.
        /// </summary>
        public static bool TryReadEvent(string envelope, out string type, out string payload)
        {
            type = null;
            payload = null;
            if (envelope == null)
            {
                return false;
            }

            using (var document = JsonDocument.Parse(envelope))
            {
                JsonElement update;
                if (!document.RootElement.TryGetProperty("event", out update) ||
                    update.ValueKind != JsonValueKind.Object)
                {
                    return false;
                }

                JsonElement member;
                if (update.TryGetProperty("type", out member) && member.ValueKind == JsonValueKind.String)
                {
                    type = member.GetString();
                }

                if (update.TryGetProperty("payload", out member))
                {
                    payload = member.GetRawText();
                }

                return type != null;
            }
        }

        /// <summary>Reads a named top-level member, or the default when absent.</summary>
        public static T Member<T>(string envelope, string name)
        {
            if (envelope == null)
            {
                return default(T);
            }

            var reader = new Utf8JsonReader(Encoding.UTF8.GetBytes(envelope));
            if (!reader.Read() || reader.TokenType != JsonTokenType.StartObject)
            {
                return default(T);
            }

            while (reader.Read() && reader.TokenType == JsonTokenType.PropertyName)
            {
                bool wanted = reader.ValueTextEquals(name);
                if (!reader.Read())
                {
                    break;
                }

                if (wanted)
                {
                    return JsonSerializer.Deserialize<T>(ref reader, Options);
                }

                reader.Skip();
            }

            return default(T);
        }
    }
}
