//
// ton-walletkit-core — SHA-2 / HMAC / PBKDF2 via CommonCrypto (internal, Apple).
//
// macOS and iOS ship CommonCrypto in libSystem, so this needs no dependency and
// no packaging: the same argument as BCrypt on Windows. The portable backend
// stays the fallback (and is ~6x slower at PBKDF2, which is the primitive the
// mnemonic loop leans on).
//
// UNVERIFIED: this compiles and runs on a Mac, which nobody has built here yet.
// twk_crypto_kat is the acceptance gate — it proves the output is bit-identical
// to the published vectors, and wrong crypto means wrong keys. If it fails,
// configure with -DTWK_CRYPTO_BACKEND=portable and open an issue.
//
#include "util/crypto.h"

#include <CommonCrypto/CommonCryptoError.h>
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonHMAC.h>
#include <CommonCrypto/CommonKeyDerivation.h>

namespace twk {
namespace crypto {
namespace {

// CommonCrypto dereferences the pointer even for a zero length, so an empty
// input gets a valid address rather than null (the KAT covers empty/empty).
const uint8_t kEmpty[1] = {0};

const uint8_t* safe(const uint8_t* data) {
    return data != nullptr ? data : kEmpty;
}

} // namespace

bool available() {
    return true;
}

bool sha256(const uint8_t* data, size_t len, uint8_t* out) {
    return CC_SHA256(safe(data), static_cast<CC_LONG>(len), out) != nullptr;
}

bool sha512(const uint8_t* data, size_t len, uint8_t* out) {
    return CC_SHA512(safe(data), static_cast<CC_LONG>(len), out) != nullptr;
}

bool hmac_sha512(const uint8_t* key, size_t key_len, const uint8_t* data, size_t data_len, uint8_t* out) {
    CCHmac(kCCHmacAlgSHA512, safe(key), key_len, safe(data), data_len, out);
    return true;
}

bool pbkdf2_sha512(const uint8_t* password, size_t password_len, const uint8_t* salt, size_t salt_len,
                   uint32_t iterations, uint8_t* out, size_t out_len) {
    if (out_len == 0) {
        return true;
    }

    const int status = CCKeyDerivationPBKDF(kCCPBKDF2, reinterpret_cast<const char*>(safe(password)),
                                            password_len, safe(salt), salt_len, kCCPRFHmacAlgSHA512,
                                            iterations, out, out_len);
    return status == kCCSuccess;
}

} // namespace crypto
} // namespace twk
