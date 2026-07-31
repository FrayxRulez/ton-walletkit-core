//
// ton-walletkit-core — the Client: ABI plumbing over the event loop (internal).
//
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "engine/event_loop.h"
#include "host_context.h"

struct twk_delegates;

namespace twk {

class JsRuntime;
class Shims;

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

private:
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

    std::mutex out_mutex_;
    std::condition_variable out_cv_;
    std::deque<std::pair<uint64_t, std::string>> out_;
    bool stopping_ = false;
    std::string receive_buffer_; // owns the string handed back by receive()

    EventLoop loop_; // declared last: started in the ctor after the above are ready
};

} // namespace twk
