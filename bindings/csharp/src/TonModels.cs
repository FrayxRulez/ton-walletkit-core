//
// Typed models for the surface the facade actually exposes.
//
// Hand-written rather than generated: only a dozen shapes are used, they are
// verified against live responses (see docs/API.md), and the consumer ships on
// .NET Native where reflection-based deserialization is unsafe.
//
using System.Globalization;
using System.Numerics;

namespace Ton.WalletKit
{
    /// <summary>
    /// An amount in nanotons. Kept as an integer rather than a decimal because TON
    /// amounts are exact integers and rounding one is a money bug.
    /// </summary>
    public readonly struct TonAmount
    {
        /// <summary>The raw value, in nanotons (1 TON = 1e9 nanotons).</summary>
        public BigInteger Nanotons { get; }

        /// <summary>Wraps a raw nanoton value.</summary>
        public TonAmount(BigInteger nanotons)
        {
            Nanotons = nanotons;
        }

        /// <summary>Parses a decimal nanoton string; zero when unparseable.</summary>
        public static TonAmount FromNanotons(string value)
        {
            return BigInteger.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed)
                ? new TonAmount(parsed)
                : new TonAmount(BigInteger.Zero);
        }

        /// <summary>The nanoton value as the ABI expects it.</summary>
        public string ToRawString() => Nanotons.ToString(CultureInfo.InvariantCulture);

        /// <summary>Whole TON, for display. Never use this for arithmetic.</summary>
        public decimal ToTon() => (decimal)Nanotons / 1_000_000_000m;

        /// <inheritdoc/>
        public override string ToString() => ToTon().ToString("0.#########", CultureInfo.InvariantCulture) + " TON";
    }

    /// <summary>
    /// Watch-only view of any address — no wallet or signer needed.
    /// Shape verified against testnet (see docs/API.md).
    /// </summary>
    public sealed class TonAccountState
    {
        /// <summary>User-friendly address.</summary>
        public string Address { get; internal set; }

        /// <summary>"active", "uninit", "frozen", …</summary>
        public string Status { get; internal set; }

        /// <summary>Balance in nanotons.</summary>
        public TonAmount Balance { get; internal set; }

        /// <summary>True when the contract is deployed and usable.</summary>
        public bool IsActive => Status == "active";

        /// <summary>Logical time of the last transaction, or null.</summary>
        public string LastTransactionLt { get; internal set; }

        /// <summary>Hash of the last transaction, or null.</summary>
        public string LastTransactionHash { get; internal set; }

        /// <summary>The raw JSON, for fields this model does not surface.</summary>
        public string RawJson { get; internal set; }
    }
}
