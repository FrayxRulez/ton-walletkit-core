//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//

package org.ton.walletkit.api;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import org.json.JSONArray;
import org.json.JSONObject;

/**
 * The reading and writing the generated models share.
 *
 * <p>{@code org.json} rather than a JSON library: it is part of the Android
 * platform, so the binding adds no dependency to the app that uses it. Nothing
 * here reflects over anything — the generated models name their own fields — so
 * R8 is free to rename and shrink whatever it likes.
 *
 * <p>Absent and null are both read as null and both written as absent. walletkit
 * distinguishes them in one direction: sending an explicit null where it expects
 * nothing gets rejected, while omitting it is always accepted.
 */
public final class TonApiJson {

    private TonApiJson() {
    }

    // ---- reading ----------------------------------------------------------

    /** The value, or null when absent or JSON null. */
    public static Object opt(JSONObject json, String key) {
        if (json == null) {
            return null;
        }
        Object value = json.opt(key);
        return value == JSONObject.NULL ? null : value;
    }

    public static String optString(JSONObject json, String key) {
        Object value = opt(json, key);
        return value == null ? null : value.toString();
    }

    public static Boolean optBoolean(JSONObject json, String key) {
        Object value = opt(json, key);
        if (value instanceof Boolean) {
            return (Boolean) value;
        }
        return value == null ? null : Boolean.valueOf(Boolean.parseBoolean(value.toString()));
    }

    public static Double optDouble(JSONObject json, String key) {
        Object value = opt(json, key);
        if (value instanceof Number) {
            return Double.valueOf(((Number) value).doubleValue());
        }
        if (value == null) {
            return null;
        }
        try {
            return Double.valueOf(Double.parseDouble(value.toString()));
        } catch (NumberFormatException notANumber) {
            return null;
        }
    }

    public static JSONObject optObject(JSONObject json, String key) {
        Object value = opt(json, key);
        return value instanceof JSONObject ? (JSONObject) value : null;
    }

    public static JSONArray optArray(JSONObject json, String key) {
        Object value = opt(json, key);
        return value instanceof JSONArray ? (JSONArray) value : null;
    }

    /** Reads an array of models. Null in, null out — an absent list is not an empty one. */
    public static <T> List<T> list(JSONArray array, Parser<T> parser) {
        if (array == null) {
            return null;
        }
        List<T> out = new ArrayList<T>(array.length());
        for (int i = 0; i < array.length(); i++) {
            Object item = array.opt(i);
            out.add(item instanceof JSONObject ? parser.parse((JSONObject) item) : null);
        }
        return out;
    }

    /** Reads an array of enum constants; unknown values become null entries. */
    public static <T> List<T> enumList(JSONArray array, EnumParser<T> parser) {
        if (array == null) {
            return null;
        }
        List<T> out = new ArrayList<T>(array.length());
        for (int i = 0; i < array.length(); i++) {
            Object item = array.opt(i);
            out.add(item == JSONObject.NULL ? null : parser.parse(item));
        }
        return out;
    }

    public static List<String> stringList(JSONArray array) {
        if (array == null) {
            return null;
        }
        List<String> out = new ArrayList<String>(array.length());
        for (int i = 0; i < array.length(); i++) {
            Object item = array.opt(i);
            out.add(item == null || item == JSONObject.NULL ? null : item.toString());
        }
        return out;
    }

    /** Reads an array of anything: values stay as org.json types. */
    public static List<Object> anyList(JSONArray array) {
        if (array == null) {
            return null;
        }
        List<Object> out = new ArrayList<Object>(array.length());
        for (int i = 0; i < array.length(); i++) {
            Object item = array.opt(i);
            out.add(item == JSONObject.NULL ? null : item);
        }
        return out;
    }

    /** Reads an object-as-map of models, keeping the wire order. */
    public static <T> Map<String, T> map(JSONObject json, Parser<T> parser) {
        if (json == null) {
            return null;
        }
        Map<String, T> out = new LinkedHashMap<String, T>();
        for (Iterator<String> keys = json.keys(); keys.hasNext();) {
            String key = keys.next();
            Object value = json.opt(key);
            out.put(key, value instanceof JSONObject ? parser.parse((JSONObject) value) : null);
        }
        return out;
    }

    public static Map<String, String> stringMap(JSONObject json) {
        if (json == null) {
            return null;
        }
        Map<String, String> out = new LinkedHashMap<String, String>();
        for (Iterator<String> keys = json.keys(); keys.hasNext();) {
            String key = keys.next();
            Object value = json.opt(key);
            out.put(key, value == null || value == JSONObject.NULL ? null : value.toString());
        }
        return out;
    }

    public static Map<String, Object> anyMap(JSONObject json) {
        if (json == null) {
            return null;
        }
        Map<String, Object> out = new LinkedHashMap<String, Object>();
        for (Iterator<String> keys = json.keys(); keys.hasNext();) {
            String key = keys.next();
            Object value = json.opt(key);
            out.put(key, value == JSONObject.NULL ? null : value);
        }
        return out;
    }

    // ---- writing ----------------------------------------------------------

    /** Writes a value, or nothing at all when it is null. */
    public static void put(JSONObject json, String key, Object value) {
        if (json == null || value == null) {
            return;
        }
        try {
            json.put(key, normalize(value));
        } catch (org.json.JSONException impossible) {
            // Only thrown for a null key or a non-finite number, neither of which
            // reaches here: keys are literals and normalize() drops NaN/Infinity.
        }
    }

    /**
     * JSON has one number type, so a whole-valued double must not be written as
     * "1.0": these values travel back into walletkit and end up in payloads that
     * get hashed and signed, where an unexpected decimal point changes the hash.
     */
    private static Object normalize(Object value) {
        if (value instanceof Double) {
            double number = ((Double) value).doubleValue();
            if (Double.isNaN(number) || Double.isInfinite(number)) {
                return null;
            }
            if (number == Math.rint(number) && Math.abs(number) < 9.007199254740992E15) {
                return Long.valueOf((long) number);
            }
        }
        return value;
    }

    /** Writes a list of plain values (strings, numbers, booleans). */
    public static JSONArray array(List<?> values) {
        if (values == null) {
            return null;
        }
        JSONArray array = new JSONArray();
        for (Object value : values) {
            array.put(value == null ? JSONObject.NULL : normalize(value));
        }
        return array;
    }

    /** Writes a map of already-converted values. */
    public static JSONObject object(Map<String, ?> values) {
        if (values == null) {
            return null;
        }
        JSONObject json = new JSONObject();
        for (Map.Entry<String, ?> entry : values.entrySet()) {
            put(json, entry.getKey(), entry.getValue());
        }
        return json;
    }
}
