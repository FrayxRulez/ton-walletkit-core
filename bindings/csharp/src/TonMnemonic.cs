//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// The values the facade hands back that are not walletkit models.
//
// Hand-written: a mnemonic is not a string (confusing it with one is how secrets
// end up in logs), and the creation result is just a pair.
//
using System.Collections.Generic;

namespace Ton.WalletKit
{
    /// <summary>A generated mnemonic and the wallet built from it (kit-ios TONWalletCreationResult).</summary>
    public sealed class TonWalletCreationResult
    {
        /// <summary>The words. Persist these: walletkit stores no keys.</summary>
        public TonMnemonic Mnemonic { get; }

        /// <summary>The adapter derived from them; register it with AddAsync.</summary>
        public ITonWalletAdapter WalletAdapter { get; }

        /// <summary>Pairs a mnemonic with the adapter derived from it.</summary>
        public TonWalletCreationResult(TonMnemonic mnemonic, ITonWalletAdapter walletAdapter)
        {
            Mnemonic = mnemonic;
            WalletAdapter = walletAdapter;
        }
    }

    /// <summary>
    /// A TON mnemonic (kit-ios TONMnemonic).
    ///
    /// This is the only thing that can rebuild a wallet, and the core never
    /// persists it — storing it securely is the app's job.
    /// </summary>
    public sealed class TonMnemonic
    {
        /// <summary>The words, in order.</summary>
        public IReadOnlyList<string> Value { get; }

        /// <summary>Wraps a word list.</summary>
        public TonMnemonic(IReadOnlyList<string> value)
        {
            Value = value;
        }

        /// <summary>The words separated by spaces, the usual storage form.</summary>
        public override string ToString()
        {
            return Value == null ? string.Empty : string.Join(" ", Value);
        }

        /// <summary>Parses a space-separated mnemonic.</summary>
        public static TonMnemonic Parse(string value)
        {
            return new TonMnemonic(value == null
                ? new string[0]
                : value.Split(new[] { ' ', '\t', '\n', '\r' }, System.StringSplitOptions.RemoveEmptyEntries));
        }
    }
}
