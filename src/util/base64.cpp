//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// ton-walletkit-core — minimal portable base64 (internal).
//
#include "util/base64.h"

#include <cstring>

namespace twk {
namespace base64 {

namespace {
const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 3 <= len; i += 3) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) | data[i + 2];
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
        out += kAlphabet[n & 63];
    }
    if (len - i == 1) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += "==";
    } else if (len - i == 2) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

std::vector<uint8_t> decode(const char* s, size_t len) {
    int8_t rev[256];
    std::memset(rev, -1, sizeof(rev));
    for (int i = 0; i < 64; ++i) {
        rev[static_cast<uint8_t>(kAlphabet[i])] = static_cast<int8_t>(i);
    }

    std::vector<uint8_t> out;
    out.reserve(len / 4 * 3);
    uint32_t buf = 0;
    int bits = 0;
    for (size_t i = 0; i < len; ++i) {
        int8_t v = rev[static_cast<uint8_t>(s[i])];
        if (v < 0) {
            continue; // skip '=', whitespace, newlines
        }
        buf = (buf << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buf >> bits) & 0xff));
        }
    }
    return out;
}

} // namespace base64
} // namespace twk
