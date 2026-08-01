//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

package org.ton.walletkit.core;

import org.ton.walletkit.api.TONNetwork;

/** A reply from the core; Gson fills these by field name. */
final class TonAdapterDescriptor {

    String adapterId;
    String address;
    String publicKey;
    String walletId;
    TONNetwork network;
}
