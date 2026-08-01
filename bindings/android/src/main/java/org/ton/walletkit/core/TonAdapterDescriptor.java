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
