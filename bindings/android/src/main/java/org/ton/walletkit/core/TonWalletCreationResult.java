//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

package org.ton.walletkit.core;

/** A generated mnemonic and the wallet derived from it (kit-ios TONWalletCreationResult). */
public final class TonWalletCreationResult {

    private final TonMnemonic mnemonic;
    private final TonWalletAdapter walletAdapter;

    TonWalletCreationResult(TonMnemonic mnemonic, TonWalletAdapter walletAdapter) {
        this.mnemonic = mnemonic;
        this.walletAdapter = walletAdapter;
    }

    /** The words. Persist these: walletkit stores no keys. */
    public TonMnemonic getMnemonic() {
        return mnemonic;
    }

    /** The adapter derived from them; register it with add(). */
    public TonWalletAdapter getWalletAdapter() {
        return walletAdapter;
    }
}
