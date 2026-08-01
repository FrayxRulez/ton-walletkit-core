//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// ton-walletkit-core — the object a QuickJS context's opaque points to (internal).
//
#pragma once

namespace twk {

class Client;
class Shims;

// Populated on the worker thread in Client::onStart and set as the context opaque,
// so native callbacks (bridge transport, shims) can reach their host objects.
// Either field may be null (e.g. `client` is null in standalone shim tests).
struct HostContext {
    Client* client = nullptr;
    Shims* shims = nullptr;
};

} // namespace twk
