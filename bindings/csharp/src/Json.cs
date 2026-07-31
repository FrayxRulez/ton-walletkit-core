//
// Minimal JSON helpers for the facade.
//
// Deliberately hand-rolled rather than pulling in a serializer: the payloads are
// small and fixed-shape, and the consumer ships on .NET Native (UWP AOT) where
// reflection-based serialization is unsafe. Reading uses a tiny scanner; writing
// only needs correct string escaping.
//
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Ton.WalletKit
{
    /// <summary>JSON escaping/extraction for the fixed-shape ABI payloads.</summary>
    internal static class Json
    {
        /// <summary>Encodes a string as a JSON literal, including the quotes.</summary>
        public static string Quote(string value)
        {
            if (value == null)
            {
                return "null";
            }

            var builder = new StringBuilder(value.Length + 2);
            builder.Append('"');
            foreach (char c in value)
            {
                switch (c)
                {
                    case '"': builder.Append("\\\""); break;
                    case '\\': builder.Append("\\\\"); break;
                    case '\b': builder.Append("\\b"); break;
                    case '\f': builder.Append("\\f"); break;
                    case '\n': builder.Append("\\n"); break;
                    case '\r': builder.Append("\\r"); break;
                    case '\t': builder.Append("\\t"); break;
                    default:
                        if (c < ' ')
                        {
                            builder.Append("\\u").Append(((int)c).ToString("x4", CultureInfo.InvariantCulture));
                        }
                        else
                        {
                            builder.Append(c);
                        }
                        break;
                }
            }
            builder.Append('"');
            return builder.ToString();
        }

        /// <summary>Builds a positional argument array: [a, b, …].</summary>
        public static string Args(params string[] rawJsonValues)
        {
            return "[" + string.Join(",", rawJsonValues) + "]";
        }

        /// <summary>
        /// Reads a top-level string property out of a <c>{"result":…}</c> envelope.
        /// Returns null when absent. Handles escapes; does not handle the key
        /// appearing inside a nested string, which these fixed payloads never do.
        /// </summary>
        public static string GetString(string json, string key)
        {
            int index = IndexOfKey(json, key);
            if (index < 0)
            {
                return null;
            }

            // Skip whitespace to the value.
            while (index < json.Length && (json[index] == ' ' || json[index] == ':'))
            {
                index++;
            }
            if (index >= json.Length || json[index] != '"')
            {
                return null; // not a string value
            }

            index++;
            var builder = new StringBuilder();
            while (index < json.Length && json[index] != '"')
            {
                if (json[index] == '\\' && index + 1 < json.Length)
                {
                    index++;
                    switch (json[index])
                    {
                        case 'n': builder.Append('\n'); break;
                        case 'r': builder.Append('\r'); break;
                        case 't': builder.Append('\t'); break;
                        case 'b': builder.Append('\b'); break;
                        case 'f': builder.Append('\f'); break;
                        case 'u':
                            if (index + 4 < json.Length)
                            {
                                builder.Append((char)Convert.ToInt32(json.Substring(index + 1, 4), 16));
                                index += 4;
                            }
                            break;
                        default: builder.Append(json[index]); break;
                    }
                }
                else
                {
                    builder.Append(json[index]);
                }
                index++;
            }
            return builder.ToString();
        }

        /// <summary>Returns the raw JSON of the "result" member, or the whole document.</summary>
        public static string Result(string json)
        {
            const string key = "{\"result\":";
            if (json != null && json.StartsWith(key, StringComparison.Ordinal))
            {
                // Strip the envelope's trailing '}'.
                return json.Substring(key.Length, json.Length - key.Length - 1);
            }
            return json;
        }

        private static int IndexOfKey(string json, string key)
        {
            if (json == null)
            {
                return -1;
            }
            string needle = "\"" + key + "\"";
            int at = json.IndexOf(needle, StringComparison.Ordinal);
            return at < 0 ? -1 : at + needle.Length;
        }
    }
}
