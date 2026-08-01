//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// Checks the generated DTOs on a desktop JVM: the shapes the emitter has to get
// right, on the models that actually use them. Returns 0 on pass.
//
// Runs against the same org.json the models use on device, so what passes here
// is what the app gets.

import java.util.ArrayList;
import java.util.List;

import org.json.JSONArray;
import org.json.JSONObject;

import org.ton.walletkit.api.TONAssetType;
import org.ton.walletkit.api.TONNetwork;
import org.ton.walletkit.api.TONSignData;
import org.ton.walletkit.api.TONSignDataText;
import org.ton.walletkit.api.TONTransactionRequest;
import org.ton.walletkit.api.TONTransactionRequestMessage;
import org.ton.walletkit.api.TonApiJson;

public final class DtoSmoke {

    private static int failures = 0;

    private static void check(boolean ok, String what, Object detail) {
        if (ok) {
            System.out.println("ok: " + what + (detail == null ? "" : " -> " + detail));
        } else {
            System.out.println("FAIL: " + what + (detail == null ? "" : " -> " + detail));
            failures++;
        }
    }

    public static void main(String[] args) {
        scalars();
        numbersAreNotDecimals();
        absentIsNotEmpty();
        nestedAndLists();
        enums();
        union();
        unknownVariantSurvives();

        System.out.println(failures == 0 ? "PASS" : "FAIL (" + failures + ")");
        System.exit(failures == 0 ? 0 : 1);
    }

    /** A round trip must not lose or invent members. */
    private static void scalars() {
        TONNetwork network = new TONNetwork().setChainId("-239");
        JSONObject json = network.toJson();
        check("-239".equals(json.optString("chainId", null)), "scalar writes", json);

        TONNetwork back = TONNetwork.fromJson(json);
        check(back != null && "-239".equals(back.getChainId()), "scalar reads back", back.getChainId());
    }

    /**
     * Numbers are held as Double because JSON has one numeric type, but a whole
     * value must not be written as "1.0": these end up in payloads that get
     * hashed and signed, where a stray decimal point changes the hash.
     */
    private static void numbersAreNotDecimals() {
        JSONObject json = new JSONObject();
        TonApiJson.put(json, "seqno", Double.valueOf(5));
        TonApiJson.put(json, "fraction", Double.valueOf(1.5));
        String text = json.toString();
        check(text.contains("\"seqno\":5") && !text.contains("5.0"), "whole numbers write without a decimal", text);
        check(text.contains("1.5"), "fractions keep their decimal", text);
    }

    /** An absent list is null, not an empty list: the two mean different things. */
    private static void absentIsNotEmpty() {
        TONTransactionRequest request = TONTransactionRequest.fromJson(new JSONObject());
        check(request != null && request.getMessages() == null, "absent list reads as null",
              request == null ? null : request.getMessages());

        JSONObject written = new TONTransactionRequest().toJson();
        check(!written.has("messages"), "null member is omitted, not written as null", written);
    }

    private static void nestedAndLists() {
        List<TONTransactionRequestMessage> messages = new ArrayList<TONTransactionRequestMessage>();
        messages.add(new TONTransactionRequestMessage().setAddress("UQC3YHz4").setAmount("1000000000"));
        messages.add(new TONTransactionRequestMessage().setAddress("UQDtFpEw").setAmount("2000000000"));

        TONTransactionRequest request = new TONTransactionRequest()
                .setMessages(messages)
                .setValidUntil(Double.valueOf(1750000000L));

        JSONObject json = request.toJson();
        JSONArray array = json.optJSONArray("messages");
        check(array != null && array.length() == 2, "list of models writes", array);
        check(array != null && "UQDtFpEw".equals(array.optJSONObject(1).optString("address", null)),
              "nested model writes its own fields", array == null ? null : array.optJSONObject(1));

        TONTransactionRequest back = TONTransactionRequest.fromJson(json);
        check(back.getMessages() != null && back.getMessages().size() == 2, "list of models reads",
              back.getMessages() == null ? null : Integer.valueOf(back.getMessages().size()));
        check("1000000000".equals(back.getMessages().get(0).getAmount()), "nested model reads",
              back.getMessages().get(0).getAmount());
    }

    private static void enums() {
        check(TONAssetType.fromValue("jetton") == TONAssetType.JETTON, "enum reads its wire value",
              TONAssetType.fromValue("jetton"));
        check("jetton".equals(TONAssetType.JETTON.getValue()), "enum writes its wire value, not its Java name",
              TONAssetType.JETTON.getValue());
        // walletkit adds enum members without a major version, so an unknown one
        // must not take the whole response down with it.
        check(TONAssetType.fromValue("something_new") == null, "unknown enum value reads as null", null);
    }

    private static void union() {
        JSONObject payload = new JSONObject();
        JSONObject value = new JSONObject();
        try {
            value.put("content", "sign me");
            payload.put("type", "text");
            payload.put("value", value);
        } catch (org.json.JSONException impossible) {
            throw new AssertionError(impossible);
        }

        TONSignData data = TONSignData.fromJson(payload);
        check(data != null && "text".equals(data.getType()), "union reads its discriminator",
              data == null ? null : data.getType());
        TONSignDataText text = data.asText();
        check(text != null, "union resolves the variant the discriminator names", text);
        check(text != null && "sign me".equals(text.getContent()), "variant carries its own fields",
              text == null ? null : text.getContent());
        check(data.asBinary() == null, "the other variants stay null", data.asBinary());

        JSONObject again = data.toJson();
        check("text".equals(again.optString("type", null))
              && again.optJSONObject("value") != null
              && "sign me".equals(again.optJSONObject("value").optString("content", null)),
              "union round trips", again);
    }

    /** A variant added upstream must arrive as raw JSON, not as an exception. */
    private static void unknownVariantSurvives() {
        JSONObject payload = new JSONObject();
        try {
            payload.put("type", "quantum");
            payload.put("value", new JSONObject().put("whatever", 1));
        } catch (org.json.JSONException impossible) {
            throw new AssertionError(impossible);
        }

        TONSignData data = TONSignData.fromJson(payload);
        check(data != null && "quantum".equals(data.getType()), "unknown variant keeps its discriminator",
              data == null ? null : data.getType());
        check(data.getValue() instanceof JSONObject, "unknown variant keeps its payload as raw JSON",
              data.getValue());
        check(data.asText() == null, "unknown variant does not masquerade as a known one", data.asText());
    }
}
