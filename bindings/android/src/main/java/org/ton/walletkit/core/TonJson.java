//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

package org.ton.walletkit.core;

import java.util.List;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import org.json.JSONTokener;

/**
 * JSON for the ABI: builds the positional argument arrays twk_send takes, and
 * opens the envelopes that come back.
 *
 * <p>{@code org.json} because it is part of Android, so the binding adds nothing
 * to the app that uses it — Telegram Android, which this exists for, takes no new
 * dependencies. The models read and write themselves (see
 * {@code org.ton.walletkit.api.TonApiJson}), so nothing here reflects.
 */
final class TonJson {

    private TonJson() {
    }

    /** A positional argument array. Nulls become JSON null, which is a position. */
    static String args(Object... values) {
        JSONArray array = new JSONArray();
        for (Object value : values) {
            array.put(encode(value));
        }
        return array.toString();
    }

    /**
     * The same, for arguments that are already JSON — wallet parameters, which
     * serialize themselves.
     */
    static String argsRaw(Object... values) {
        JSONArray array = new JSONArray();
        for (Object value : values) {
            if (value instanceof String && isJson((String) value)) {
                array.put(parse((String) value));
            } else {
                array.put(encode(value));
            }
        }
        return array.toString();
    }

    /** The signing options object an optional fakeSignature becomes. */
    static String signOptions(Boolean fakeSignature) {
        if (fakeSignature == null) {
            return null;
        }
        JSONObject options = new JSONObject();
        try {
            options.put("fakeSignature", fakeSignature.booleanValue());
        } catch (JSONException impossible) {
            return null;
        }
        return options.toString();
    }

    /** The envelope's result member, or null when there is none. */
    static Object result(String envelope) {
        if (envelope == null) {
            return null;
        }
        Object root = parse(envelope);
        if (!(root instanceof JSONObject)) {
            return null;
        }
        Object result = ((JSONObject) root).opt("result");
        return result == JSONObject.NULL ? null : result;
    }

    static JSONObject resultObject(String envelope) {
        Object value = result(envelope);
        return value instanceof JSONObject ? (JSONObject) value : null;
    }

    static JSONArray resultArray(String envelope) {
        Object value = result(envelope);
        return value instanceof JSONArray ? (JSONArray) value : null;
    }

    static String resultString(String envelope) {
        Object value = result(envelope);
        return value == null ? null : value.toString();
    }

    /** Splits an {@code {"event":{"type":…,"payload":…}}} envelope. */
    static JSONObject event(String envelope) {
        if (envelope == null) {
            return null;
        }
        Object root = parse(envelope);
        if (!(root instanceof JSONObject)) {
            return null;
        }
        Object update = ((JSONObject) root).opt("event");
        return update instanceof JSONObject ? (JSONObject) update : null;
    }

    /** Values that reach the ABI: primitives, string lists, and models. */
    private static Object encode(Object value) {
        if (value == null) {
            return JSONObject.NULL;
        }
        if (value instanceof List) {
            JSONArray array = new JSONArray();
            for (Object item : (List<?>) value) {
                array.put(encode(item));
            }
            return array;
        }
        if (value instanceof byte[]) {
            // walletkit's JS reads keys as arrays of numbers; base64 would be
            // silently accepted and produce the wrong key.
            byte[] bytes = (byte[]) value;
            JSONArray array = new JSONArray();
            for (byte b : bytes) {
                array.put(b & 0xff);
            }
            return array;
        }
        return value;
    }

    private static Object parse(String text) {
        try {
            return new JSONTokener(text).nextValue();
        } catch (JSONException malformed) {
            return null;
        }
    }

    private static boolean isJson(String value) {
        String trimmed = value.trim();
        return trimmed.startsWith("{") || trimmed.startsWith("[");
    }
}
