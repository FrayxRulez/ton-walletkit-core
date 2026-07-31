//
// The one value type the generated models do not give us.
//
// walletkit represents amounts as decimal strings (TokenAmount), which is right
// on the wire and wrong in an app: this keeps them exact.
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
}
