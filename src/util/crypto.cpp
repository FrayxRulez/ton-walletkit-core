//
// ton-walletkit-core — platform crypto primitives (internal).
//
#include "util/crypto.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <bcrypt.h>
#else
#include <random>
#endif

namespace twk {
namespace crypto {

#if defined(_WIN32)

namespace {

// Algorithm providers are opened once and reused: the TON mnemonic loop performs
// hundreds of hashes, and re-opening per call dominated the cost (createMnemonic
// went 21.9s -> 3.4s when PBKDF2's provider was first cached). BCrypt algorithm
// handles are safe for concurrent use by the hashing APIs.
BCRYPT_ALG_HANDLE open_alg(LPCWSTR id, DWORD flags) {
    BCRYPT_ALG_HANDLE handle = nullptr;
    if (BCryptOpenAlgorithmProvider(&handle, id, nullptr, flags) != 0) {
        return nullptr;
    }
    return handle;
}

BCRYPT_ALG_HANDLE sha256_alg() {
    static BCRYPT_ALG_HANDLE h = open_alg(BCRYPT_SHA256_ALGORITHM, 0);
    return h;
}

BCRYPT_ALG_HANDLE sha512_alg() {
    static BCRYPT_ALG_HANDLE h = open_alg(BCRYPT_SHA512_ALGORITHM, 0);
    return h;
}

BCRYPT_ALG_HANDLE hmac_sha512_alg() {
    static BCRYPT_ALG_HANDLE h = open_alg(BCRYPT_SHA512_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    return h;
}

// One-shot hash / HMAC (secret == nullptr for a plain digest).
bool bcrypt_hash(BCRYPT_ALG_HANDLE alg, const uint8_t* secret, size_t secret_len, const uint8_t* data,
                 size_t data_len, uint8_t* out, size_t out_len) {
    if (alg == nullptr) {
        return false;
    }
    return BCryptHash(alg, const_cast<PUCHAR>(secret), static_cast<ULONG>(secret_len),
                      const_cast<PUCHAR>(data), static_cast<ULONG>(data_len), out,
                      static_cast<ULONG>(out_len)) == 0;
}

} // namespace

bool available() {
    return sha256_alg() != nullptr && sha512_alg() != nullptr && hmac_sha512_alg() != nullptr;
}

bool random_bytes(uint8_t* out, size_t len) {
    if (len == 0) {
        return true;
    }
    return BCryptGenRandom(nullptr, out, static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

bool sha256(const uint8_t* data, size_t len, uint8_t* out) {
    return bcrypt_hash(sha256_alg(), nullptr, 0, data, len, out, 32);
}

bool sha512(const uint8_t* data, size_t len, uint8_t* out) {
    return bcrypt_hash(sha512_alg(), nullptr, 0, data, len, out, 64);
}

bool hmac_sha512(const uint8_t* key, size_t key_len, const uint8_t* data, size_t data_len, uint8_t* out) {
    return bcrypt_hash(hmac_sha512_alg(), key, key_len, data, data_len, out, 64);
}

bool pbkdf2_sha512(const uint8_t* password, size_t password_len, const uint8_t* salt, size_t salt_len,
                   uint32_t iterations, uint8_t* out, size_t out_len) {
    BCRYPT_ALG_HANDLE alg = hmac_sha512_alg();
    if (alg == nullptr || out_len == 0) {
        return alg != nullptr;
    }
    return BCryptDeriveKeyPBKDF2(alg, const_cast<PUCHAR>(password), static_cast<ULONG>(password_len),
                                 const_cast<PUCHAR>(salt), static_cast<ULONG>(salt_len), iterations, out,
                                 static_cast<ULONG>(out_len), 0) == 0;
}

#else

// Hashing is not yet implemented off Windows: the JS side falls back to its
// pure-JS implementations. The desktop reference host (M2) wires OpenSSL here.
// Randomness is available everywhere (std::random_device is backed by the OS
// entropy source on the platforms we target).
bool available() {
    return false;
}
bool random_bytes(uint8_t* out, size_t len) {
    static std::random_device rd;
    for (size_t i = 0; i < len; ++i) {
        out[i] = static_cast<uint8_t>(rd() & 0xff);
    }
    return true;
}
bool sha256(const uint8_t*, size_t, uint8_t*) {
    return false;
}
bool sha512(const uint8_t*, size_t, uint8_t*) {
    return false;
}
bool hmac_sha512(const uint8_t*, size_t, const uint8_t*, size_t, uint8_t*) {
    return false;
}
bool pbkdf2_sha512(const uint8_t*, size_t, const uint8_t*, size_t, uint32_t, uint8_t*, size_t) {
    return false;
}

#endif

} // namespace crypto
} // namespace twk
