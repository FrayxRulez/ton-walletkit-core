//
// ton-walletkit-core — the Client + the C ABI entry points.
//
#include "client.h"

#include <chrono>
#include <unordered_set>

#include "bridge/transport.h"
#include "engine/js_runtime.h"
#include "shims/shims.h"
#include "twk/twk.h"
#include "twk/twk_delegates.h"

namespace twk {

namespace {

// Live-client registry.
//
// Host completions (twk_http_respond and friends) arrive on the host's own
// threads and can race twk_client_destroy — an in-flight request naturally
// completes after the app has torn the client down. Validating the handle here
// makes that safe: destruction removes the client under this lock before it tears
// anything down, and completions hold the lock for the whole call, so a stale
// handle is simply not found and is ignored instead of dereferenced.
std::mutex& registry_mutex() {
    static std::mutex m;
    return m;
}

std::unordered_set<Client*>& live_clients() {
    static std::unordered_set<Client*> set;
    return set;
}

} // namespace

void Client::registerLive(Client* client) {
    std::lock_guard<std::mutex> guard(registry_mutex());
    live_clients().insert(client);
}

void Client::unregisterLive(Client* client) {
    std::lock_guard<std::mutex> guard(registry_mutex());
    live_clients().erase(client);
}

void Client::withLive(twk_client* handle, const std::function<void(Client&)>& fn) {
    auto* client = reinterpret_cast<Client*>(handle);
    std::lock_guard<std::mutex> guard(registry_mutex());
    if (live_clients().count(client) == 0) {
        return; // destroyed (or never valid) — drop the late completion
    }
    fn(*client);
}

Client::Client(const twk_delegates* delegates, void* user) : delegates_(delegates), user_(user) {
    EventLoop::Hooks hooks;
    hooks.on_start = [this] { onStart(); };
    hooks.after_work = [this] { afterWork(); };
    hooks.on_stop = [this] { onStop(); };
    loop_.start(std::move(hooks));

    registerLive(this);
}

Client::~Client() {
    // Leave the registry first: from here on, host completions can't reach us, so
    // nothing can touch this object while it tears down.
    unregisterLive(this);

    {
        std::lock_guard<std::mutex> guard(out_mutex_);
        stopping_ = true;
    }
    out_cv_.notify_all();  // release a blocked receive()
    loop_.stop();          // joins the worker thread (runs onStop -> frees js_)

    // Drop in-flight delegate calls: any completion arriving after this is for a
    // token we no longer know about and is ignored.
    std::lock_guard<std::mutex> guard(tokens_mutex_);
    http_pending_.clear();
}

// ---- host delegates -------------------------------------------------------

int64_t Client::startHttp(const std::string& method, const std::string& url, const std::string& headers_json,
                          const std::string& body, std::function<void(HttpResult)> on_done) {
    if (delegates_ == nullptr || delegates_->http_request == nullptr) {
        HttpResult result;
        result.error = "no http_request delegate installed";
        on_done(std::move(result));
        return 0;
    }

    int64_t token;
    {
        std::lock_guard<std::mutex> guard(tokens_mutex_);
        token = next_token_++;
        http_pending_[token] = std::move(on_done);
    }

    delegates_->http_request(user_, reinterpret_cast<twk_client*>(this), token, method.c_str(), url.c_str(),
                             headers_json.empty() ? nullptr : headers_json.c_str(),
                             body.empty() ? nullptr : body.c_str());
    return token;
}

void Client::cancelHttp(int64_t token) {
    bool was_pending;
    {
        std::lock_guard<std::mutex> guard(tokens_mutex_);
        was_pending = http_pending_.erase(token) > 0;
    }
    if (was_pending && delegates_ != nullptr && delegates_->http_cancel != nullptr) {
        delegates_->http_cancel(user_, reinterpret_cast<twk_client*>(this), token);
    }
}

void Client::completeHttp(int64_t token, HttpResult result) {
    std::function<void(HttpResult)> handler;
    {
        std::lock_guard<std::mutex> guard(tokens_mutex_);
        auto it = http_pending_.find(token);
        if (it == http_pending_.end()) {
            return; // unknown, cancelled, or the client is tearing down
        }
        handler = std::move(it->second);
        http_pending_.erase(it);
    }

    // Hop to the worker thread: the handler touches JS state.
    loop_.post([handler = std::move(handler), result = std::move(result)]() mutable { handler(std::move(result)); });
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

// ---- host -> core completions --------------------------------------------

void twk_http_respond(twk_client* client, twk_token token, int status, const char* headers_json,
                      const char* body) {
    if (client == nullptr) {
        return;
    }
    twk::HttpResult result;
    result.ok = true;
    result.status = status;
    result.headers_json = headers_json != nullptr ? headers_json : "";
    result.body = body != nullptr ? body : "";
    // withLive: safe if the client was destroyed while this request was in flight.
    twk::Client::withLive(client, [&](twk::Client& c) { c.completeHttp(token, std::move(result)); });
}

void twk_http_failed(twk_client* client, twk_token token, const char* error) {
    if (client == nullptr) {
        return;
    }
    twk::HttpResult result;
    result.ok = false;
    result.error = error != nullptr ? error : "request failed";
    twk::Client::withLive(client, [&](twk::Client& c) { c.completeHttp(token, std::move(result)); });
}

// SSE (M4) and storage (M3) completions — accepted but not yet routed.
void twk_sse_event(twk_client*, twk_token, const char*) {}
void twk_sse_closed(twk_client*, twk_token, const char*) {}
void twk_storage_respond(twk_client*, twk_token, const char*) {}

} // extern "C"
