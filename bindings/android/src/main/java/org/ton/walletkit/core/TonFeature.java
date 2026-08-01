//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

package org.ton.walletkit.core;

import java.util.List;

/** A TON Connect feature this wallet supports. */
public final class TonFeature {

    public String name;

    /** SendTransaction: how many messages one transaction may carry. */
    public Integer maxMessages;

    /** SendTransaction: whether extra currencies may be attached. */
    public Boolean extraCurrencySupported;

    /** SignData: the payload types accepted. */
    public List<String> types;

    public TonFeature() {
    }

    public TonFeature(String name) {
        this.name = name;
    }
}
