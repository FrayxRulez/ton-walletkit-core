//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// ton-walletkit-core — host shims: the "just an engine" gap for QuickJS (internal).
//
// Registers the globals the bundled JS expects but QuickJS does not provide:
// console, setTimeout/clearTimeout/setInterval/clearInterval (via the EventLoop),
// crypto.getRandomValues (CSPRNG) and window/Pbkdf2.derive (PBKDF2-HMAC-SHA512).
// Mirrors kit-ios JSCore/Polyfilling; RNG + PBKDF2 route to platform crypto here
// and become host delegates in M2.
//
#pragma once

#include <cstdint>
#include <unordered_map>

#include "quickjs.h"

namespace twk {

class JsRuntime;
class EventLoop;

class Shims {
public:
    Shims(JsRuntime& js, EventLoop& loop);
    ~Shims();

    Shims(const Shims&) = delete;
    Shims& operator=(const Shims&) = delete;

    // Register all host globals on the context. Call once, on the worker thread,
    // before the bundle is evaluated.
    void install();

    // Timer bookkeeping (worker-thread only). Used by the setTimeout/setInterval
    // shims; returns an id usable with clearTimer.
    uint64_t addTimer(JSValueConst fn, int64_t delay_ms, bool repeat);
    void clearTimer(uint64_t id);

private:
    void fireTimer(uint64_t id);

    JsRuntime& js_;
    EventLoop& loop_;

    struct TimerEntry {
        JSValue fn;
        bool repeat;
        uint64_t loop_id;
    };
    std::unordered_map<uint64_t, TimerEntry> timers_;
    uint64_t next_timer_id_ = 1;
};

} // namespace twk
