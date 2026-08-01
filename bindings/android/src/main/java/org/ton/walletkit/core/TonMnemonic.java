//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

package org.ton.walletkit.core;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

/**
 * A TON mnemonic.
 *
 * <p>Wrapped rather than passed as a String so it cannot be mistaken for prose
 * and logged. It is the only thing that can rebuild a wallet, and the core never
 * persists it — storing it securely is the app's job.
 */
public final class TonMnemonic {

    private final List<String> value;

    public TonMnemonic(List<String> value) {
        this.value = Collections.unmodifiableList(new ArrayList<String>(value));
    }

    public static TonMnemonic parse(String value) {
        // "\\s+", not "\s+": the latter is a Java 15 string escape meaning one
        // literal space, so it compiled on a modern JDK and split on spaces
        // alone — a mnemonic pasted with a newline or tab would have come back
        // with a word glued to the next one.
        return new TonMnemonic(Arrays.asList(value.trim().split("\\s+")));
    }

    public List<String> getValue() {
        return value;
    }

    /** The words separated by spaces — the usual storage form. Never log this. */
    public String joined() {
        StringBuilder out = new StringBuilder();
        for (String word : value) {
            out.append(out.length() == 0 ? "" : " ").append(word);
        }
        return out.toString();
    }

    /** Deliberately not the words: toString ends up in logs. */
    @Override
    public String toString() {
        return "TonMnemonic(" + value.size() + " words)";
    }
}
