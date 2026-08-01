//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// Reference host — a test-only implementation of the twk delegates.
//
// Lets the core be exercised end to end (twk-cli, tests) against real endpoints
// with no platform integration: HTTP is served by the OS HTTP stack rather than
// TDLib. Windows uses WinHTTP (in-box, no external dependency); other platforms
// need a libcurl backend, which is where the #else branch goes.
//
// Not part of the shipped library.
//
#pragma once

#include <memory>
#include <string>

#include "twk/twk.h"
#include "twk/twk_delegates.h"

namespace twk {
namespace refhost {

class ReferenceHost {
public:
    // `storage_file` (optional) persists storage to disk so data survives a
    // client — and a process — the way PasswordVault/Keychain would. Empty means
    // in-memory only.
    explicit ReferenceHost(std::string storage_file = {});
    ~ReferenceHost();

    ReferenceHost(const ReferenceHost&) = delete;
    ReferenceHost& operator=(const ReferenceHost&) = delete;

    // Pass to twk_client_create(delegates(), userData()).
    const twk_delegates* delegates() const { return &delegates_; }
    void* userData();

    // True when the platform backend is compiled in (false => http_request always
    // fails, so tests can skip networking instead of hanging).
    static bool httpAvailable();

    // Public so the delegate thunks can reach it; not part of the tested surface.
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
    twk_delegates delegates_{};
};

} // namespace refhost
} // namespace twk
