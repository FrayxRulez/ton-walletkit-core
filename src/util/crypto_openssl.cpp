//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// ton-walletkit-core — SHA-2 / HMAC / PBKDF2 via OpenSSL (internal).
//
// For platforms where OpenSSL is genuinely part of the environment: Linux
// desktops, and Telegram Desktop, which already links it. Selected only when
// CMake finds it, because it is emphatically NOT part of the environment on
// Android or iOS — there the portable backend is the right answer.
//
// UNVERIFIED: no OpenSSL development package is installed on the machine this
// was written on. twk_crypto_kat is the acceptance gate; if it fails, configure
// with -DTWK_CRYPTO_BACKEND=portable and open an issue.
//
#include "util/crypto.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

namespace twk {
namespace crypto {
namespace {

// OpenSSL is not consistent about null pointers with zero lengths across
// versions, so empty inputs get a valid address instead.
const uint8_t kEmpty[1] = {0};

const uint8_t* safe(const uint8_t* data) {
    return data != nullptr ? data : kEmpty;
}

} // namespace

bool available() {
    return true;
}

bool sha256(const uint8_t* data, size_t len, uint8_t* out) {
    return SHA256(safe(data), len, out) != nullptr;
}

bool sha512(const uint8_t* data, size_t len, uint8_t* out) {
    return SHA512(safe(data), len, out) != nullptr;
}

bool hmac_sha512(const uint8_t* key, size_t key_len, const uint8_t* data, size_t data_len, uint8_t* out) {
    unsigned int written = 0;
    const unsigned char* mac = HMAC(EVP_sha512(), safe(key), static_cast<int>(key_len), safe(data), data_len,
                                    out, &written);
    return mac != nullptr && written == 64;
}

bool pbkdf2_sha512(const uint8_t* password, size_t password_len, const uint8_t* salt, size_t salt_len,
                   uint32_t iterations, uint8_t* out, size_t out_len) {
    if (out_len == 0) {
        return true;
    }

    return PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(safe(password)), static_cast<int>(password_len),
                             safe(salt), static_cast<int>(salt_len), static_cast<int>(iterations),
                             EVP_sha512(), static_cast<int>(out_len), out) == 1;
}

} // namespace crypto
} // namespace twk
