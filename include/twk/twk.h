//
// ton-walletkit-core — public C ABI.
//
// A client embeds the canonical @ton/walletkit JS in QuickJS and exposes a small
// JSON send/receive interface. The domain payload is JSON; request/response
// correlation is a native `request_id` (never spliced into the JSON as @extra).
// See ARCHITECTURE.md.
//
#ifndef TWK_H
#define TWK_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#if defined(TWK_BUILDING_SHARED)
#define TWK_EXPORT __declspec(dllexport)
#elif defined(TWK_USING_SHARED)
#define TWK_EXPORT __declspec(dllimport)
#else
#define TWK_EXPORT
#endif
#else
#if defined(TWK_BUILDING_SHARED)
#define TWK_EXPORT __attribute__((visibility("default")))
#else
#define TWK_EXPORT
#endif
#endif

// Opaque client handle. Each owns a QuickJS runtime + worker thread.
typedef struct twk_client twk_client;

// Host delegates (HTTP / SSE / storage / RNG / log). Defined in twk_delegates.h
// and wired in the networking milestone (M2); may be NULL until then.
typedef struct twk_delegates twk_delegates;

// Create a client. `delegates`/`user` are retained for host callbacks (unused
// until M2; pass NULL for now). Returns NULL on failure.
TWK_EXPORT twk_client* twk_client_create(const twk_delegates* delegates, void* user);

// Destroy a client: stops the worker thread and frees the runtime. After this
// the handle must not be used, and no delegate completions may reference it.
TWK_EXPORT void twk_client_destroy(twk_client* client);

// Send a request. `request_id` is a caller-chosen correlation id echoed back by
// receive on the response (use a nonzero value). `method` is the walletkit bridge
// method; `params_json` is its arguments as JSON (object | array | string |
// number), or NULL/"" for none. Asynchronous: the response arrives via receive.
TWK_EXPORT void twk_send(twk_client* client, unsigned long long request_id, const char* method,
                         const char* params_json);

// Receive the next message for this client (a response or an unsolicited update).
// Blocks up to `timeout` seconds (<= 0 polls without blocking). Returns a JSON
// string owned by the client and valid until the next receive on this thread, or
// NULL on timeout. On return, *request_id is the correlated id (0 for updates).
// Must not be called concurrently from two threads for the same client.
TWK_EXPORT const char* twk_receive(twk_client* client, double timeout, unsigned long long* request_id);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // TWK_H
