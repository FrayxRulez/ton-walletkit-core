package org.ton.walletkit.core;

/** An error the core returned for a request. */
public final class WalletKitException extends Exception {

    private static final long serialVersionUID = 1L;

    private final String json;

    WalletKitException(String message, String json) {
        super(message);
        this.json = json;
    }

    /** The raw {@code {"error":{…}}} envelope, when there was one. */
    public String getJson() {
        return json;
    }

    /**
     * Pulls "message" out of an error envelope without a JSON parser — the
     * transport must not depend on whichever one the app uses.
     */
    static String messageOf(String json) {
        final String key = "\"message\":\"";
        int start = json.indexOf(key);
        if (start < 0) {
            return "walletkit error";
        }
        start += key.length();
        int end = json.indexOf('"', start);
        return end < 0 ? "walletkit error" : json.substring(start, end);
    }
}
