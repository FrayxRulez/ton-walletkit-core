//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// ton-walletkit-core — minimal portable base64 (internal).
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace twk {
namespace base64 {

std::string encode(const uint8_t* data, size_t len);
std::vector<uint8_t> decode(const char* s, size_t len);

} // namespace base64
} // namespace twk
