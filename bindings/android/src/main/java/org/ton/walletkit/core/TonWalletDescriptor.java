package org.ton.walletkit.core;

import org.ton.walletkit.api.TONNetwork;

/** A reply from the core; Gson fills these by field name. */
final class TonWalletDescriptor {

    String walletId;
    String address;
    String publicKey;
    String version;
    TONNetwork network;
}
