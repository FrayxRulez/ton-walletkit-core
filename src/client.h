//
// ton-walletkit-core — the Client: ABI plumbing over the event loop (internal).
//
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "engine/event_loop.h"
#include "host_context.h"

struct twk_delegates;
struct twk_client; // opaque handle from the public ABI (see include/twk/twk.h)

namespace twk {

class JsRuntime;
class Shims;

// Outcome of a host HTTP call, handed back to whoever registered the token.
struct HttpResult {
    bool ok = false;         // false => transport failure, `error` is set
    int status = 0;          // HTTP status when ok
    std::string headers_json;
    std::string body;
    std::string error;
};

// One client = one QuickJS runtime on one worker thread + an output queue.
//
// send() posts a request onto the worker thread; the response/updates flow back
// through emit() onto the output queue that receive() drains. Correlation is the
// native request_id (0 for unsolicited updates).
class Client {
public:
    Client(const twk_delegates* delegates, void* user);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    void send(uint64_t request_id, std::string method, std::string params_json);
    const char* receive(double timeout_seconds, uint64_t* request_id);

    // Push a message onto the output queue. Called on the worker thread (by the
    // bridge transport, or the M0 placeholder in handleCall).
    void emit(uint64_t request_id, std::string json);

    JsRuntime* js() const { return js_; }
    Shims& shims() const { return *shims_; }

    // Called from the __twk_ready host global (worker thread) when the bundle has
    // finished loading; flushes any calls queued before readiness.
    void onJsReady();

    // ---- host delegates ---------------------------------------------------

    // Start an HTTP request via the host. `on_done` runs on the worker thread when
    // the host completes it. Returns the token (usable with cancelHttp), or 0 if
    // no http_request delegate is installed (on_done is then invoked immediately
    // with a failure).
    int64_t startHttp(const std::string& method, const std::string& url, const std::string& headers_json,
                      const std::string& body, std::function<void(HttpResult)> on_done);

    void cancelHttp(int64_t token);

    // Called from twk_http_respond / twk_http_failed on any thread.
    void completeHttp(int64_t token, HttpResult result);

    // Storage. `on_done(found, value)` runs on the worker thread; for set/remove/
    // clear the arguments are meaningless and only signal completion. Without a
    // storage delegate these fail closed (get -> not found, writes -> done), so
    // the kit still runs with nothing persisted.
    enum class StorageOp { Get, Set, Remove, Clear };
    void storage(StorageOp op, const std::string& key, const std::string& value,
                 std::function<void(bool found, std::string value)> on_done);

    // Called from twk_storage_respond on any thread.
    void completeStorage(int64_t token, bool found, std::string value);

    // Runs `fn` only if `handle` is still a live client, holding the registry lock
    // for the duration so it cannot be destroyed mid-call. Host completions arrive
    // on host threads and can legitimately race destroy; this makes that safe
    // instead of a use-after-free.
    static void withLive(twk_client* handle, const std::function<void(Client&)>& fn);

private:
    static void registerLive(Client* client);
    static void unregisterLive(Client* client);

    void onStart();
    void afterWork();
    void onStop();
    void handleCall(uint64_t request_id, const std::string& method, const std::string& params_json);

    const twk_delegates* delegates_;
    void* user_;

    JsRuntime* js_ = nullptr;              // created/destroyed on the worker thread
    std::unique_ptr<Shims> shims_;         // ditto (worker thread lifetime)
    HostContext host_context_;             // set as the JS context opaque

    // Calls received before the bundle signals readiness are queued here (worker
    // thread only) and flushed by onJsReady().
    bool js_ready_ = false;
    struct PendingCall {
        uint64_t request_id;
        std::string method;
        std::string params_json;
    };
    std::deque<PendingCall> pending_;

    // In-flight delegate calls. Guarded by its own mutex because completions
    // arrive on arbitrary host threads; cleared on teardown so late completions
    // for a destroyed client are dropped.
    std::mutex tokens_mutex_;
    int64_t next_token_ = 1;
    std::unordered_map<int64_t, std::function<void(HttpResult)>> http_pending_;
    std::unordered_map<int64_t, std::function<void(bool, std::string)>> storage_pending_;

    std::mutex out_mutex_;
    std::condition_variable out_cv_;
    std::deque<std::pair<uint64_t, std::string>> out_;
    bool stopping_ = false;
    std::string receive_buffer_; // owns the string handed back by receive()

    EventLoop loop_; // declared last: started in the ctor after the above are ready
};

} // namespace twk
