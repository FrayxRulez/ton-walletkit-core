//
// ton-walletkit-core — host delegates.
//
// The core performs no I/O of its own: HTTP, SSE and secure storage are provided
// by the embedding host. Windows backs HTTP with TDLib `sendTonCenterApiRequest`
// and storage with PasswordVault; the desktop reference host uses libcurl and a
// file/memory store. See ARCHITECTURE.md.
//
// Threading: delegate callbacks are invoked on the client's worker thread and
// must not block — start the work and return. Completions (twk_*_respond) may be
// called from any thread; the core marshals them onto the client's queue.
//
// Lifetime: a completion may race twk_client_destroy — an in-flight request
// naturally finishes after the app tore the client down, and the host's network
// thread cannot know that. The core keeps a registry of live clients and
// validates the handle, so completing on a destroyed client is safe and simply
// does nothing. (This applies only to the twk_*_respond family; ordinary calls
// such as twk_send/twk_receive must not be used after destroy.)
//
#ifndef TWK_DELEGATES_H
#define TWK_DELEGATES_H

#include <stddef.h>
#include <stdint.h>

#include "twk.h"

#ifdef __cplusplus
extern "C" {
#endif

// Correlates a core-initiated delegate call with its completion. Distinct from
// the ABI's request_id, which correlates an app request with its response.
typedef int64_t twk_token;

struct twk_delegates {
    // ---- HTTP ----------------------------------------------------------
    // A thin JSON transport: `method` is "GET" or "POST", `url` is the absolute
    // URL walletkit built, `headers_json` is a JSON object of request headers
    // (may be NULL), `body` is the request body for POST (NULL otherwise).
    // Complete with twk_http_respond, or twk_http_failed on transport failure.
    //
    // TDLib hosts split `url` into endpoint path + query/payload — see
    // ARCHITECTURE.md "Mapping http_request -> TDLib".
    void (*http_request)(void* user, twk_client* client, twk_token token, const char* method, const char* url,
                         const char* headers_json, const char* body);

    // Best-effort cancellation (AbortController / timeout). The host may ignore
    // it; a late completion for a cancelled token is discarded.
    void (*http_cancel)(void* user, twk_client* client, twk_token token);

    // ---- SSE (the TON Connect relay) ------------------------------------
    // Multi-shot, unlike http_request: one open yields many twk_sse_event calls
    // and exactly one twk_sse_closed. The host owns the text/event-stream framing
    // and delivers one dispatched event per call as a JSON object:
    //   {"data":"…","event":"message","id":"…"}
    // (`id` matters — TON Connect resumes from lastEventId after a reconnect.)
    void (*sse_open)(void* user, twk_client* client, twk_token token, const char* url, const char* headers_json);
    void (*sse_close)(void* user, twk_client* client, twk_token token);

    // ---- Secure storage -------------------------------------------------
    // Holds wallet metadata and secrets (PasswordVault on Windows, Keychain on
    // iOS, Keystore on Android; a file/memory store in the reference host).
    //
    // All four are asynchronous: walletkit awaits every storage operation, so a
    // host doing real I/O must acknowledge completion or "wallet added" can
    // resolve before it is actually persisted. Complete each with
    // twk_storage_respond — for get, pass the value (or NULL when absent); for
    // set/remove/clear the value is ignored and only signals "done".
    void (*storage_get)(void* user, twk_client* client, twk_token token, const char* key);
    void (*storage_set)(void* user, twk_client* client, twk_token token, const char* key, const char* value);
    void (*storage_remove)(void* user, twk_client* client, twk_token token, const char* key);
    void (*storage_clear)(void* user, twk_client* client, twk_token token);

    // ---- Diagnostics ----------------------------------------------------
    // level: 0=debug 1=info 2=warn 3=error. May be NULL (the core logs to stderr).
    void (*log)(void* user, twk_client* client, int level, const char* message);
};

// ---- host -> core completions ------------------------------------------
// Safe to call from any thread. Unknown/stale tokens are ignored.

// Deliver an HTTP response. `headers_json` may be NULL; `body` may be NULL for an
// empty body. The strings are copied — the caller keeps ownership.
TWK_EXPORT void twk_http_respond(twk_client* client, twk_token token, int status, const char* headers_json,
                                 const char* body);

// Fail an HTTP request (connection error, timeout, cancellation).
TWK_EXPORT void twk_http_failed(twk_client* client, twk_token token, const char* error);

// SSE (M4) and storage (M3) completions.
TWK_EXPORT void twk_sse_event(twk_client* client, twk_token token, const char* data);
TWK_EXPORT void twk_sse_closed(twk_client* client, twk_token token, const char* error /* NULL = normal */);
TWK_EXPORT void twk_storage_respond(twk_client* client, twk_token token, const char* value /* NULL = missing */);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // TWK_DELEGATES_H
