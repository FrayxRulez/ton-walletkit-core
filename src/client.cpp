//
// ton-walletkit-core — the Client + the C ABI entry points.
//
#include "client.h"

#include <chrono>

#include "engine/js_runtime.h"
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
    js_->setOpaque(this);
    // The bridge bundle + host globals are installed here in the next task.
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
    delete js_;
    js_ = nullptr;
}

void Client::send(uint64_t request_id, std::string method, std::string params_json) {
    loop_.post([this, request_id, method = std::move(method), params_json = std::move(params_json)] {
        handleCall(request_id, method, params_json);
    });
}

void Client::handleCall(uint64_t request_id, const std::string& method, const std::string& params_json) {
    // M0 placeholder: echo the call back as a result. Replaced by the bridge
    // transport (native -> JS handleNativeCall -> JS -> native __twk_emit) next.
    std::string result = "{\"result\":{\"method\":\"";
    result += method;
    result += "\",\"params\":";
    result += params_json.empty() ? "null" : params_json;
    result += "}}";
    emit(request_id, std::move(result));
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
