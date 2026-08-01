//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

package org.ton.walletkit.api;

/**
 * Reads one enum constant from its wire value.
 *
 * <p>Exists because {@code values()} is a static member and cannot be reached
 * through a type parameter: an array of enums needs some object to do the
 * lookup, so every generated enum exposes one as {@code VALUES}.
 *
 * @param <T> the enum this reads
 */
public interface EnumParser<T> {

    /** @return null when the value is absent or is not a known constant */
    T parse(Object wire);
}
