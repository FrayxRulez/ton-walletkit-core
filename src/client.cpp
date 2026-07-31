//
// ton-walletkit-core — the Client + the C ABI entry points.
//
#include "client.h"

#include <chrono>

#include "bridge/transport.h"
#include "engine/js_runtime.h"
#include "shims/shims.h"
#include "twk/twk.h"

namespace twk {

Client::Client(const twk_delegates* delegates, void* user) : delegates_(delegates), user_(user) {
    EventLoop::Hooks hooks;
    hooks.on_start = [this] { onStart(); };
    hooks.after_work = [this] { afterWork(); };
    hooks.on_stop = [this] { onStop(); };
    loop_.start(std::move(hooks));
}

Client::~Client() {
    {
        std::lock_guard<std::mutex> guard(out_mutex_);
        stopping_ = true;
    }
    out_cv_.notify_all();  // release a blocked receive()
    loop_.stop();          // joins the worker thread (runs onStop -> frees js_)
}

void Client::onStart() {
    js_ = new JsRuntime();

    // Expose the host objects to native callbacks via the context opaque.
    shims_ = std::make_unique<Shims>(*js_, loop_);
    host_context_.client = this;
    host_context_.shims = shims_.get();
    js_->setOpaque(&host_context_);

    // Host globals (console/timers/crypto/Pbkdf2) must exist before the bundle runs.
    shims_->install();

    // Install the transport (__twk_ready) and evaluate the bundle.
    std::string error;
    if (!bridge::install(*js_, &error)) {
        // Nothing to route the failure to yet (no request in flight). The load-error
        // path is wired with the real bundle.
    }

    // The bundle bootstraps asynchronously (dynamic imports resolve via the job
    // queue). Pump it so walletKit is constructed and __twk_ready fires before the
    // first request is dispatched.
    afterWork();
}

void Client::afterWork() {
    if (!js_) {
        return;
    }
    // Drain the QuickJS job queue (promise reactions, etc.).
    JSContext* ctx = nullptr;
    while (JS_ExecutePendingJob(js_->runtime(), &ctx) > 0) {
    }
}

void Client::onStop() {
    shims_.reset(); // frees timer JSValues while the context is still alive (worker thread)
    delete js_;
    js_ = nullptr;
}

void Client::send(uint64_t request_id, std::string method, std::string params_json) {
    loop_.post([this, request_id, method = std::move(method), params_json = std::move(params_json)] {
        handleCall(request_id, method, params_json);
    });
}

void Client::handleCall(uint64_t request_id, const std::string& method, const std::string& params_json) {
    // native -> JS: walletKit[method](...args), awaited natively. Queue until the
    // bundle signals readiness.
    if (js_ready_) {
        bridge::dispatch(*js_, *this, request_id, method, params_json);
    } else {
        pending_.push_back({request_id, method, params_json});
    }
}

void Client::onJsReady() {
    js_ready_ = true;
    std::deque<PendingCall> pending = std::move(pending_);
    pending_.clear();
    for (auto& call : pending) {
        bridge::dispatch(*js_, *this, call.request_id, call.method, call.params_json);
    }
}

void Client::emit(uint64_t request_id, std::string json) {
    {
        std::lock_guard<std::mutex> guard(out_mutex_);
        out_.emplace_back(request_id, std::move(json));
    }
    out_cv_.notify_one();
}

const char* Client::receive(double timeout_seconds, uint64_t* request_id) {
    std::unique_lock<std::mutex> lock(out_mutex_);
    if (out_.empty()) {
        if (timeout_seconds > 0) {
            out_cv_.wait_for(lock, std::chrono::duration<double>(timeout_seconds),
                             [this] { return !out_.empty() || stopping_; });
        }
        if (out_.empty()) {
            return nullptr;
        }
    }

    auto item = std::move(out_.front());
    out_.pop_front();
    lock.unlock();

    if (request_id) {
        *request_id = item.first;
    }
    receive_buffer_ = std::move(item.second);
    return receive_buffer_.c_str();
}

} // namespace twk

// ---- C ABI ---------------------------------------------------------------

extern "C" {

twk_client* twk_client_create(const twk_delegates* delegates, void* user) {
    return reinterpret_cast<twk_client*>(new twk::Client(delegates, user));
}

void twk_client_destroy(twk_client* client) {
    delete reinterpret_cast<twk::Client*>(client);
}

void twk_send(twk_client* client, unsigned long long request_id, const char* method, const char* params_json) {
    reinterpret_cast<twk::Client*>(client)->send(request_id, method ? method : "",
                                                 params_json ? params_json : "");
}

const char* twk_receive(twk_client* client, double timeout, unsigned long long* request_id) {
    uint64_t rid = 0;
    const char* out = reinterpret_cast<twk::Client*>(client)->receive(timeout, &rid);
    if (request_id) {
        *request_id = rid;
    }
    return out;
}

} // extern "C"
