package org.ton.walletkit.core;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

/**
 * JSON for the ABI: builds the positional argument arrays twk_send takes, and
 * reads result envelopes into the generated DTOs.
 *
 * <p>Gson because Android has no reflection-free requirement (unlike .NET
 * Native, where the C# binding needs hand-written converters) and because it is
 * the smallest mainstream option openapi-generator emits for. It is a dependency
 * the consuming app gains — see docs/BINDINGS.md.
 */
final class TonJson {

    /** Nulls are dropped rather than sent: walletkit treats absent and null differently. */
    static final Gson GSON = new GsonBuilder().disableHtmlEscaping().create();

    private TonJson() {
    }

    /** A positional argument array. Values are serialized by their runtime type. */
    static String args(Object... values) {
        JsonArray array = new JsonArray();
        for (Object value : values) {
            array.add(GSON.toJsonTree(value));
        }
        return array.toString();
    }

    /**
     * The same, for arguments that are already JSON — wallet parameters, which
     * serialize themselves. A null element becomes JSON null.
     */
    static String argsRaw(Object... values) {
        JsonArray array = new JsonArray();
        for (Object value : values) {
            if (value instanceof String && isJson((String) value)) {
                array.add(JsonParser.parseString((String) value));
            } else {
                array.add(GSON.toJsonTree(value));
            }
        }
        return array.toString();
    }

    /** The signing options object an optional fakeSignature becomes. */
    static String signOptions(Boolean fakeSignature) {
        if (fakeSignature == null) {
            return null;
        }
        JsonObject options = new JsonObject();
        options.addProperty("fakeSignature", fakeSignature);
        return options.toString();
    }

    /** The value of the envelope's result member, or null when there is none. */
    static JsonElement result(String envelope) {
        if (envelope == null) {
            return null;
        }
        JsonElement root = JsonParser.parseString(envelope);
        if (!root.isJsonObject()) {
            return null;
        }
        JsonElement result = root.getAsJsonObject().get("result");
        return result == null || result.isJsonNull() ? null : result;
    }

    static <T> T result(String envelope, Class<T> type) {
        JsonElement value = result(envelope);
        return value == null ? null : GSON.fromJson(value, type);
    }

    /** Splits an {@code {"event":{"type":…,"payload":…}}} envelope. */
    static JsonObject event(String envelope) {
        JsonElement root = JsonParser.parseString(envelope);
        if (!root.isJsonObject()) {
            return null;
        }
        JsonElement update = root.getAsJsonObject().get("event");
        return update != null && update.isJsonObject() ? update.getAsJsonObject() : null;
    }

    private static boolean isJson(String value) {
        String trimmed = value.trim();
        return trimmed.startsWith("{") || trimmed.startsWith("[");
    }
}
