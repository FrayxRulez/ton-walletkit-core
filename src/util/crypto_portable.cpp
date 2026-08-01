//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// ton-walletkit-core — portable SHA-2 / HMAC / PBKDF2 (internal).
//
// The backend for every platform without one of its own. It exists because the
// alternative on those platforms was falling back to the pure-JS hashes, which
// cost ~49 ms per hmac_sha512 in the interpreter and made creating a mnemonic
// take 16.7 seconds.
//
// Self-contained on purpose: OpenSSL is not present on Android or iOS, and
// depending on it would trade a portability problem for a packaging one. The
// implementations are the textbook FIPS 180-4 / RFC 2104 / RFC 8018 ones, and
// twk_crypto_kat proves them bit-identical to the published vectors — which is
// the only thing that matters here, since wrong crypto means wrong keys.
//
#include "util/crypto.h"

#include <cstring>

namespace twk {
namespace crypto {
namespace {

inline uint32_t rotr32(uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

inline uint64_t rotr64(uint64_t value, int bits) {
    return (value >> bits) | (value << (64 - bits));
}

// ---- SHA-256 ---------------------------------------------------------------

const uint32_t kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

struct Sha256 {
    static const size_t kBlockSize = 64;
    static const size_t kDigestSize = 32;

    uint32_t state[8];
    uint64_t length; // message length in bytes
    uint8_t buffer[kBlockSize];
    size_t buffered;

    Sha256() : length(0), buffered(0) {
        state[0] = 0x6a09e667u;
        state[1] = 0xbb67ae85u;
        state[2] = 0x3c6ef372u;
        state[3] = 0xa54ff53au;
        state[4] = 0x510e527fu;
        state[5] = 0x9b05688cu;
        state[6] = 0x1f83d9abu;
        state[7] = 0x5be0cd19u;
        std::memset(buffer, 0, sizeof(buffer));
    }

    void compress(const uint8_t* block) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | static_cast<uint32_t>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = h + s1 + ch + kSha256K[i] + w[i];
            const uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = s0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    void update(const uint8_t* data, size_t len) {
        length += len;
        while (len > 0) {
            const size_t take = (kBlockSize - buffered) < len ? (kBlockSize - buffered) : len;
            std::memcpy(buffer + buffered, data, take);
            buffered += take;
            data += take;
            len -= take;
            if (buffered == kBlockSize) {
                compress(buffer);
                buffered = 0;
            }
        }
    }

    void finish(uint8_t* out) {
        const uint64_t bits = length * 8;
        const uint8_t padding = 0x80;
        update(&padding, 1);
        const uint8_t zero = 0;
        while (buffered != 56) {
            update(&zero, 1);
        }
        for (int i = 7; i >= 0; --i) {
            const uint8_t byte = static_cast<uint8_t>((bits >> (i * 8)) & 0xff);
            std::memcpy(buffer + buffered, &byte, 1);
            ++buffered;
        }
        compress(buffer);
        buffered = 0;

        for (int i = 0; i < 8; ++i) {
            out[i * 4] = static_cast<uint8_t>(state[i] >> 24);
            out[i * 4 + 1] = static_cast<uint8_t>(state[i] >> 16);
            out[i * 4 + 2] = static_cast<uint8_t>(state[i] >> 8);
            out[i * 4 + 3] = static_cast<uint8_t>(state[i]);
        }
    }
};

// ---- SHA-512 ---------------------------------------------------------------

const uint64_t kSha512K[80] = {
    0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull,
    0x3956c25bf348b538ull, 0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull,
    0xd807aa98a3030242ull, 0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
    0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull, 0xc19bf174cf692694ull,
    0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
    0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
    0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull,
    0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull, 0x06ca6351e003826full, 0x142929670a0e6e70ull,
    0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
    0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
    0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull,
    0xd192e819d6ef5218ull, 0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
    0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull,
    0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull,
    0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
    0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull,
    0xca273eceea26619cull, 0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull,
    0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
    0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull,
    0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull,
};

struct Sha512 {
    static const size_t kBlockSize = 128;
    static const size_t kDigestSize = 64;

    uint64_t state[8];
    uint64_t length; // message length in bytes (the high half is never needed here)
    uint8_t buffer[kBlockSize];
    size_t buffered;

    Sha512() : length(0), buffered(0) {
        state[0] = 0x6a09e667f3bcc908ull;
        state[1] = 0xbb67ae8584caa73bull;
        state[2] = 0x3c6ef372fe94f82bull;
        state[3] = 0xa54ff53a5f1d36f1ull;
        state[4] = 0x510e527fade682d1ull;
        state[5] = 0x9b05688c2b3e6c1full;
        state[6] = 0x1f83d9abfb41bd6bull;
        state[7] = 0x5be0cd19137e2179ull;
        std::memset(buffer, 0, sizeof(buffer));
    }

    void compress(const uint8_t* block) {
        uint64_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = 0;
            for (int b = 0; b < 8; ++b) {
                w[i] = (w[i] << 8) | block[i * 8 + b];
            }
        }
        for (int i = 16; i < 80; ++i) {
            const uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
            const uint64_t s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint64_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint64_t e = state[4], f = state[5], g = state[6], h = state[7];

        for (int i = 0; i < 80; ++i) {
            const uint64_t s1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
            const uint64_t ch = (e & f) ^ (~e & g);
            const uint64_t t1 = h + s1 + ch + kSha512K[i] + w[i];
            const uint64_t s0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
            const uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint64_t t2 = s0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    void update(const uint8_t* data, size_t len) {
        length += len;
        while (len > 0) {
            const size_t take = (kBlockSize - buffered) < len ? (kBlockSize - buffered) : len;
            std::memcpy(buffer + buffered, data, take);
            buffered += take;
            data += take;
            len -= take;
            if (buffered == kBlockSize) {
                compress(buffer);
                buffered = 0;
            }
        }
    }

    void finish(uint8_t* out) {
        const uint64_t bits = length * 8;
        const uint8_t padding = 0x80;
        update(&padding, 1);
        const uint8_t zero = 0;
        while (buffered != 112) {
            update(&zero, 1);
        }
        // 128-bit length; the high 64 bits are zero for any message we hash.
        std::memset(buffer + buffered, 0, 8);
        buffered += 8;
        for (int i = 7; i >= 0; --i) {
            buffer[buffered++] = static_cast<uint8_t>((bits >> (i * 8)) & 0xff);
        }
        compress(buffer);
        buffered = 0;

        for (int i = 0; i < 8; ++i) {
            for (int b = 0; b < 8; ++b) {
                out[i * 8 + b] = static_cast<uint8_t>(state[i] >> (56 - b * 8));
            }
        }
    }
};

// ---- HMAC-SHA512 (RFC 2104) ------------------------------------------------

struct HmacSha512 {
    Sha512 inner;
    uint8_t outer_key[Sha512::kBlockSize];

    HmacSha512(const uint8_t* key, size_t key_len) {
        uint8_t padded[Sha512::kBlockSize];
        std::memset(padded, 0, sizeof(padded));

        if (key_len > Sha512::kBlockSize) {
            Sha512 shrink;
            shrink.update(key, key_len);
            shrink.finish(padded);
        } else if (key_len > 0) {
            std::memcpy(padded, key, key_len);
        }

        uint8_t inner_key[Sha512::kBlockSize];
        for (size_t i = 0; i < Sha512::kBlockSize; ++i) {
            inner_key[i] = static_cast<uint8_t>(padded[i] ^ 0x36);
            outer_key[i] = static_cast<uint8_t>(padded[i] ^ 0x5c);
        }
        inner.update(inner_key, sizeof(inner_key));
    }

    void update(const uint8_t* data, size_t len) {
        if (len > 0) {
            inner.update(data, len);
        }
    }

    void finish(uint8_t* out) {
        uint8_t digest[Sha512::kDigestSize];
        inner.finish(digest);

        Sha512 outer;
        outer.update(outer_key, sizeof(outer_key));
        outer.update(digest, sizeof(digest));
        outer.finish(out);
    }
};

} // namespace

bool available() {
    return true;
}

bool sha256(const uint8_t* data, size_t len, uint8_t* out) {
    Sha256 hash;
    if (len > 0) {
        hash.update(data, len);
    }
    hash.finish(out);
    return true;
}

bool sha512(const uint8_t* data, size_t len, uint8_t* out) {
    Sha512 hash;
    if (len > 0) {
        hash.update(data, len);
    }
    hash.finish(out);
    return true;
}

bool hmac_sha512(const uint8_t* key, size_t key_len, const uint8_t* data, size_t data_len, uint8_t* out) {
    HmacSha512 mac(key, key_len);
    mac.update(data, data_len);
    mac.finish(out);
    return true;
}

bool pbkdf2_sha512(const uint8_t* password, size_t password_len, const uint8_t* salt, size_t salt_len,
                   uint32_t iterations, uint8_t* out, size_t out_len) {
    if (iterations == 0) {
        return false;
    }

    const size_t hash_len = Sha512::kDigestSize;
    uint8_t block[Sha512::kDigestSize];
    uint8_t running[Sha512::kDigestSize];
    uint32_t index = 1;

    while (out_len > 0) {
        // U1 = PRF(password, salt || INT_32_BE(index))
        const uint8_t counter[4] = {
            static_cast<uint8_t>(index >> 24),
            static_cast<uint8_t>(index >> 16),
            static_cast<uint8_t>(index >> 8),
            static_cast<uint8_t>(index),
        };

        HmacSha512 first(password, password_len);
        first.update(salt, salt_len);
        first.update(counter, sizeof(counter));
        first.finish(block);
        std::memcpy(running, block, hash_len);

        for (uint32_t i = 1; i < iterations; ++i) {
            HmacSha512 next(password, password_len);
            next.update(running, hash_len);
            next.finish(running);
            for (size_t b = 0; b < hash_len; ++b) {
                block[b] ^= running[b];
            }
        }

        const size_t take = out_len < hash_len ? out_len : hash_len;
        std::memcpy(out, block, take);
        out += take;
        out_len -= take;
        ++index;
    }

    return true;
}

} // namespace crypto
} // namespace twk
