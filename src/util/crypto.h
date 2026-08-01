//
// ton-walletkit-core — platform crypto primitives (internal).
//
// Backed by the platform: BCrypt on Windows, CommonCrypto on Apple, OpenSSL when
// CMake finds it, and a self-contained implementation everywhere else (Android
// has no stable crypto C API). One backend per build, chosen by
// TWK_CRYPTO_BACKEND — see src/util/crypto_*.cpp, and README's platform table.
//
// These exist because the pure-JS implementations (jssha) are far too slow in the
// QuickJS interpreter: hmac_sha512 measured ~49 ms/call vs ~0.02 ms natively.
//
// twk_crypto_kat must pass for whichever backend is selected: a fast, wrong hash
// produces wrong keys in silence.
//
// M2 note: these are process-wide platform calls, not host delegates — unlike
// HTTP/SSE/storage, hashing has no reason to cross the delegate boundary.
//
#pragma once

#include <cstddef>
#include <cstdint>

namespace twk {
namespace crypto {

// True when the platform backend is available; when false the JS side falls back
// to its pure-JS implementations.
bool available();

bool random_bytes(uint8_t* out, size_t len);

// out must have room for 32 / 64 bytes respectively.
bool sha256(const uint8_t* data, size_t len, uint8_t* out);
bool sha512(const uint8_t* data, size_t len, uint8_t* out);
bool hmac_sha512(const uint8_t* key, size_t key_len, const uint8_t* data, size_t data_len, uint8_t* out);

bool pbkdf2_sha512(const uint8_t* password, size_t password_len, const uint8_t* salt, size_t salt_len,
                   uint32_t iterations, uint8_t* out, size_t out_len);

} // namespace crypto
} // namespace twk
