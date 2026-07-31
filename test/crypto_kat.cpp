// Known-answer tests for the native crypto backing the wallet's key derivation.
// These MUST match the reference implementations exactly — a mismatch would
// silently produce wrong mnemonics/keys. Vectors are the published RFC ones
// (SHA-256 "abc"; RFC 4231 HMAC-SHA512 case 1) plus values generated with
// Node/OpenSSL. Returns 0 on pass.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "util/crypto.h"

using namespace twk;

static std::string hex(const uint8_t* data, size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 0x0f];
    }
    return out;
}

static bool check(const char* name, const std::string& got, const char* want) {
    if (got != want) {
        printf("FAIL %s\n  got  %s\n  want %s\n", name, got.c_str(), want);
        return false;
    }
    printf("ok: %s\n", name);
    return true;
}

int main() {
    if (!crypto::available()) {
        printf("SKIP: native crypto unavailable on this platform\n");
        return 0; // the JS side falls back to pure JS; nothing to verify here
    }

    bool ok = true;
    const uint8_t abc[] = {'a', 'b', 'c'};

    uint8_t d256[32];
    ok &= crypto::sha256(abc, 3, d256);
    ok &= check("sha256(\"abc\")", hex(d256, 32),
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    uint8_t d512[64];
    ok &= crypto::sha512(abc, 3, d512);
    ok &= check("sha512(\"abc\")", hex(d512, 64),
                "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
                "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");

    // RFC 4231 test case 1: key = 20 x 0x0b, data = "Hi There".
    std::vector<uint8_t> key(20, 0x0b);
    const uint8_t hi[] = {'H', 'i', ' ', 'T', 'h', 'e', 'r', 'e'};
    uint8_t mac[64];
    ok &= crypto::hmac_sha512(key.data(), key.size(), hi, sizeof(hi), mac);
    ok &= check("hmac_sha512(rfc4231 #1)", hex(mac, 64),
                "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cde"
                "daa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854");

    // Empty key + empty data (edge case: null pointers must be handled).
    uint8_t mac0[64];
    ok &= crypto::hmac_sha512(nullptr, 0, nullptr, 0, mac0);
    ok &= check("hmac_sha512(empty, empty)", hex(mac0, 64),
                "b936cee86c9f87aa5d3c6f2e84cb5a4239a5fe50480a6ec66b70ab5b1f4ac673"
                "0c6c515421b327ec1d69402e53dfb49ad7381eb067b338fd7b0cb22247225d47");

    const uint8_t password[] = {'p', 'a', 's', 's', 'w', 'o', 'r', 'd'};
    const uint8_t salt[] = {'s', 'a', 'l', 't'};
    uint8_t dk[64];
    ok &= crypto::pbkdf2_sha512(password, sizeof(password), salt, sizeof(salt), 1, dk, sizeof(dk));
    ok &= check("pbkdf2_sha512(1 iter)", hex(dk, 64),
                "867f70cf1ade02cff3752599a3a53dc4af34c7a669815ae5d513554e1c8cf252"
                "c02d470a285a0501bad999bfe943c08f050235d7d68b1da55e63f73b60a57fce");

    // The salt/iteration count the TON mnemonic loop actually uses.
    const char* ton_salt = "TON seed version";
    uint8_t dk2[64];
    ok &= crypto::pbkdf2_sha512(password, sizeof(password), reinterpret_cast<const uint8_t*>(ton_salt),
                                std::strlen(ton_salt), 390, dk2, sizeof(dk2));
    ok &= check("pbkdf2_sha512(390 iters, TON salt)", hex(dk2, 64),
                "f1f491bda09709288f8b1c0557ad061b5d661e338e4ab56056ac7d27f020ba49"
                "41f49b00114db4a1720a44ac7ca6e24991eabfe02424f5516ca2708958b2008c");

    // RNG: must fill and not return a constant block.
    uint8_t r1[32] = {0}, r2[32] = {0};
    ok &= crypto::random_bytes(r1, sizeof(r1));
    ok &= crypto::random_bytes(r2, sizeof(r2));
    bool nonzero = false;
    for (uint8_t b : r1) {
        nonzero = nonzero || b != 0;
    }
    if (!nonzero || std::memcmp(r1, r2, sizeof(r1)) == 0) {
        printf("FAIL random_bytes: zero or repeating output\n");
        ok = false;
    } else {
        printf("ok: random_bytes\n");
    }

    return ok ? 0 : 1;
}
