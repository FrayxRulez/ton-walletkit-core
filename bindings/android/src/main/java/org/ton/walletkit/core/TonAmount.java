package org.ton.walletkit.core;

import java.math.BigDecimal;
import java.math.BigInteger;

/**
 * An amount in nanotons.
 *
 * <p>An integer, not a double: TON amounts are exact and rounding one is a money
 * bug. walletkit sends them as decimal strings for the same reason.
 */
public final class TonAmount {

    private static final BigDecimal NANO = new BigDecimal("1000000000");

    private final BigInteger nanotons;

    public TonAmount(BigInteger nanotons) {
        this.nanotons = nanotons == null ? BigInteger.ZERO : nanotons;
    }

    /** Parses a decimal nanoton string; zero when unparseable. */
    public static TonAmount fromNanotons(String value) {
        try {
            return new TonAmount(new BigInteger(value));
        } catch (RuntimeException malformed) {
            return new TonAmount(BigInteger.ZERO);
        }
    }

    public BigInteger getNanotons() {
        return nanotons;
    }

    /** The value as the ABI expects it. */
    public String toRawString() {
        return nanotons.toString();
    }

    /** Whole TON, for display. Never use this for arithmetic. */
    public BigDecimal toTon() {
        return new BigDecimal(nanotons).divide(NANO);
    }

    @Override
    public String toString() {
        return toTon().stripTrailingZeros().toPlainString() + " TON";
    }
}
