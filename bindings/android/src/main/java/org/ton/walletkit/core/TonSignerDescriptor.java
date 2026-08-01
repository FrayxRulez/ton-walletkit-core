package org.ton.walletkit.core;

import org.ton.walletkit.api.TONNetwork;

/** A reply from the core; Gson fills these by field name. */
final class TonSignerDescriptor {

    String signerId;
    String publicKey;
}
