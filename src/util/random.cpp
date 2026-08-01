//
// ton-walletkit-core — the platform entropy source (internal).
//
// Separate from the hash backend because it is the one primitive that must come
// from the OS on every platform: this is what seeds mnemonics and TON Connect
// session keys, and a predictable byte here is a stolen wallet.
//
// Deliberately not std::random_device, which this used to be off Windows: the
// standard permits it to be a deterministic PRNG, and some implementations
// (notably older MinGW) really are — the same "random" mnemonic every run.
//
#include "util/crypto.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <bcrypt.h>
#elif defined(__APPLE__)
#include <sys/random.h>
#elif defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#else
#include <cstdlib>
#endif

#if !defined(_WIN32)
#include <cstdio>
#endif

namespace twk {
namespace crypto {
namespace {

#if !defined(_WIN32)
// Last resort, and still a real entropy source: every platform we target keeps
// /dev/urandom, and a short read is treated as failure rather than papered over.
bool read_urandom(uint8_t* out, size_t len) {
    std::FILE* file = std::fopen("/dev/urandom", "rb");
    if (file == nullptr) {
        return false;
    }
    const size_t got = std::fread(out, 1, len, file);
    std::fclose(file);
    return got == len;
}
#endif

} // namespace

bool random_bytes(uint8_t* out, size_t len) {
    if (len == 0) {
        return true;
    }

#if defined(_WIN32)
    return BCryptGenRandom(nullptr, out, static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;

#elif defined(__APPLE__)
    // getentropy caps at 256 bytes per call, so loop.
    while (len > 0) {
        const size_t take = len < 256 ? len : 256;
        if (getentropy(out, take) != 0) {
            return read_urandom(out, len);
        }
        out += take;
        len -= take;
    }
    return true;

#elif defined(__linux__)
    while (len > 0) {
#if defined(SYS_getrandom)
        // The syscall rather than the wrapper: bionic only declares getrandom()
        // from API 28, and Telegram Android builds against 21. The syscall has
        // been there since Linux 3.17 either way.
        const ssize_t got = static_cast<ssize_t>(syscall(SYS_getrandom, out, len, 0));
#else
        const ssize_t got = -1;
        errno = ENOSYS;
#endif
        if (got < 0) {
            if (errno == EINTR) {
                continue; // interrupted before any bytes were read
            }
            return read_urandom(out, len);
        }
        out += got;
        len -= static_cast<size_t>(got);
    }
    return true;

#else
    return read_urandom(out, len);
#endif
}

} // namespace crypto
} // namespace twk
