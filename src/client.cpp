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
    // nothing can touch this object while it tears down. withLive holds the
    // registry lock across the whole completion, so this also waits out any that
    // is mid-flight — meaning no host thread is still posting once the loop stops.
    unregisterLive(this);

    {
        std::lock_guard<std::mutex> guard(out_mutex_);
        stopping_ = true;
    }
    out_cv_.notify_all();  // release a blocked receive()
    loop_.stop();          // joins the worker thread (runs onStop -> frees js_)

    // onStop already released the handler maps on the worker thread, which is the
    // only thread allowed to free the JS values they hold; this is the belt to
    // that braces, and must find nothing left to destroy.
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

int64_t Client::openSse(const std::string& url, const std::string& headers_json,
                        std::function<void(std::string)> on_event,
                        std::function<void(std::string, bool)> on_closed) {
    if (delegates_ == nullptr || delegates_->sse_open == nullptr) {
        on_closed("no sse_open delegate installed", true);
        return 0;
    }

    int64_t token;
    {
        std::lock_guard<std::mutex> guard(tokens_mutex_);
        token = next_token_++;
        sse_streams_[token] = SseHandlers{std::move(on_event), std::move(on_closed)};
    }

    delegates_->sse_open(user_, reinterpret_cast<twk_client*>(this), token, url.c_str(),
                         headers_json.empty() ? nullptr : headers_json.c_str());
    return token;
}

void Client::closeSse(int64_t token) {
    bool was_open;
    {
        std::lock_guard<std::mutex> guard(tokens_mutex_);
        was_open = sse_streams_.erase(token) > 0;
    }
    if (was_open && delegates_ != nullptr && delegates_->sse_close != nullptr) {
        delegates_->sse_close(user_, reinterpret_cast<twk_client*>(this), token);
    }
}

void Client::completeSseEvent(int64_t token, std::string data) {
    std::function<void(std::string)> handler;
    {
        std::lock_guard<std::mutex> guard(tokens_mutex_);
        auto it = sse_streams_.find(token);
        if (it == sse_streams_.end()) {
            return; // closed, or the client is tearing down
        }
        handler = it->second.on_event; // copy: the stream stays open for more events
    }
    loop_.post([handler = std::move(handler), data = std::move(data)]() mutable { handler(std::move(data)); });
}

void Client::completeSseClosed(int64_t token, std::string error, bool had_error) {
    SseHandlers handlers;
    {
        std::lock_guard<std::mutex> guard(tokens_mutex_);
        auto it = sse_streams_.find(token);
        if (it == sse_streams_.end()) {
            return;
        }
        // The whole entry moves out, not just on_closed: on_event holds a JS
        // callback too, and erasing it here would run JS_FreeValue on this host
        // thread while the worker is executing JS. Both handlers ride to the
        // worker in the task below and are destroyed there when it returns.
        handlers = std::move(it->second);
        sse_streams_.erase(it); // terminal; the node now holds moved-from handlers
    }
    loop_.post([handlers = std::move(handlers), error = std::move(error), had_error]() mutable {
        handlers.on_closed(std::move(error), had_error);
    });
}

void Client::storage(StorageOp op, const std::string& key, const std::string& value,
                     std::function<void(bool, std::string)> on_done) {
    const bool have_delegate =
        delegates_ != nullptr &&
        ((op == StorageOp::Get && delegates_->storage_get != nullptr) ||
         (op == StorageOp::Set && delegates_->storage_set != nullptr) ||
         (op == StorageOp::Remove && delegates_->storage_remove != nullptr) ||
         (op == StorageOp::Clear && delegates_->storage_clear != nullptr));

    if (!have_delegate) {
        // Fail closed: reads miss, writes report done. The kit still works, just
        // without persistence.
        on_done(false, std::string());
        return;
    }

    int64_t token;
    {
        std::lock_guard<std::mutex> guard(tokens_mutex_);
        token = next_token_++;
        storage_pending_[token] = std::move(on_done);
    }

    auto* handle = reinterpret_cast<twk_client*>(this);
    switch (op) {
        case StorageOp::Get:
            delegates_->storage_get(user_, handle, token, key.c_str());
            break;
        case StorageOp::Set:
            delegates_->storage_set(user_, handle, token, key.c_str(), value.c_str());
            break;
        case StorageOp::Remove:
            delegates_->storage_remove(user_, handle, token, key.c_str());
            break;
        case StorageOp::Clear:
            delegates_->storage_clear(user_, handle, token);
            break;
    }
}

void Client::completeStorage(int64_t token, bool found, std::string value) {
    std::function<void(bool, std::string)> handler;
    {
        std::lock_guard<std::mutex> guard(tokens_mutex_);
        auto it = storage_pending_.find(token);
        if (it == storage_pending_.end()) {
            return;
        }
        handler = std::move(it->second);
        storage_pending_.erase(it);
    }
    loop_.post([handler = std::move(handler), found, value = std::move(value)]() mutable {
        handler(found, std::move(value));
    });
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
    // Drain the QuickJS job queue (promise reactions, etc.). Bounded for the same
    // reason as dispatch: a promise reaction can loop forever too.
    JsDeadline deadline(*js_);
    JSContext* ctx = nullptr;
    while (JS_ExecutePendingJob(js_->runtime(), &ctx) > 0) {
    }
}

void Client::onStop() {
    // Pending HTTP handlers can hold JSValues (the fetch shim's callback), so they
    // must be released here — on the worker thread, while the context is still
    // alive. Dropping them in ~Client would free JSValues after the runtime is gone.
    {
        std::lock_guard<std::mutex> guard(tokens_mutex_);
        http_pending_.clear();
        storage_pending_.clear();
        sse_streams_.clear();
    }
    shims_.reset(); // frees timer JSValues while the context is still alive
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

void twk_storage_respond(twk_client* client, twk_token token, const char* value) {
    if (client == nullptr) {
        return;
    }
    const bool found = value != nullptr;
    std::string copy = found ? value : "";
    twk::Client::withLive(client,
                          [&](twk::Client& c) { c.completeStorage(token, found, std::move(copy)); });
}

void twk_sse_event(twk_client* client, twk_token token, const char* data) {
    if (client == nullptr) {
        return;
    }
    std::string copy = data != nullptr ? data : "";
    twk::Client::withLive(client, [&](twk::Client& c) { c.completeSseEvent(token, std::move(copy)); });
}

void twk_sse_closed(twk_client* client, twk_token token, const char* error) {
    if (client == nullptr) {
        return;
    }
    const bool had_error = error != nullptr;
    std::string copy = had_error ? error : "";
    twk::Client::withLive(client,
                          [&](twk::Client& c) { c.completeSseClosed(token, std::move(copy), had_error); });
}

} // extern "C"
