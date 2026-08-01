//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

package org.ton.walletkit.api;

import org.json.JSONObject;

/**
 * Builds one model from JSON.
 *
 * <p>Every generated model exposes its own as a {@code PARSER} constant. That is
 * what replaces handing a {@code Class} to a reflective mapper: the call site
 * names the parser, the compiler checks it, and R8 renaming a field cannot
 * silently turn it into null.
 *
 * @param <T> the model this builds
 */
public interface Parser<T> {

    /** @param json may be null, in which case implementations return null */
    T parse(JSONObject json);
}
