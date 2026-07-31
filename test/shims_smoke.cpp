// Exercises the host shims (console / crypto.getRandomValues / Pbkdf2.derive /
// timers) on a worker-thread JsRuntime + Shims, mirroring the real client setup
// (EventLoop + HostContext). Returns 0 on pass.
#include <chrono>
#include <cstdio>
#include <future>
#include <string>

#include "engine/event_loop.h"
#include "engine/js_runtime.h"
#include "host_context.h"
#include "shims/shims.h"

using namespace twk;
using namespace std::chrono_literals;

// Runs JS on a dedicated worker thread with the shims installed, like Client does.
class Harness {
public:
    void start() {
        EventLoop::Hooks hooks;
        hooks.on_start = [this] {
            js_ = new JsRuntime();
            shims_ = new Shims(*js_, loop_);
            ctx_.shims = shims_;
            js_->setOpaque(&ctx_);
            shims_->install();
        };
        hooks.after_work = [this] {
            if (js_ != nullptr) {
                JSContext* c = nullptr;
                while (JS_ExecutePendingJob(js_->runtime(), &c) > 0) {
                }
            }
        };
        hooks.on_stop = [this] {
            delete shims_;
            delete js_;
        };
        loop_.start(std::move(hooks));
    }

    // Evaluate `code` on the worker thread; returns JSON.stringify(result) or "ERR:...".
    std::string eval(const std::string& code) {
        std::promise<std::string> p;
        auto fut = p.get_future();
        loop_.post([this, &p, code] {
            std::string result, error;
            if (!js_->eval(code, "<shims_smoke>", &result, &error)) {
                p.set_value("ERR:" + error);
            } else {
                p.set_value(result);
            }
        });
        return fut.get();
    }

    ~Harness() { loop_.stop(); }

private:
    EventLoop loop_;
    JsRuntime* js_ = nullptr;
    Shims* shims_ = nullptr;
    HostContext ctx_;
};

static bool expect(const char* name, const std::string& got, const std::string& want) {
    if (got != want) {
        printf("FAIL %s: got %s, want %s\n", name, got.c_str(), want.c_str());
        return false;
    }
    printf("ok: %s\n", name);
    return true;
}

int main() {
    Harness h;
    h.start();

    bool ok = true;

    // console: must not throw.
    ok &= expect("console", h.eval("(()=>{ console.log('hi', 1, {a:2}); console.error('e'); return 'ok'; })()"),
                 "\"ok\"");

    // crypto.getRandomValues: fills the array (not all zero) and returns it.
    ok &= expect("getRandomValues",
                 h.eval("(()=>{ const a=new Uint8Array(32); const r=crypto.getRandomValues(a); "
                        "return (r===a && a.some(x=>x!==0)) ? 'ok':'bad'; })()"),
                 "\"ok\"");

    // Pbkdf2.derive: deterministic, salt-sensitive, non-empty. ('cGFzcw=='='pass',
    // 'c2FsdA=='='salt', 'U0FMVA=='='SALT')
    ok &= expect("pbkdf2",
                 h.eval("(()=>{ const a=Pbkdf2.derive('cGFzcw==','c2FsdA==',64,32,'sha-512'); "
                        "const b=Pbkdf2.derive('cGFzcw==','c2FsdA==',64,32,'sha-512'); "
                        "const c=Pbkdf2.derive('cGFzcw==','U0FMVA==',64,32,'sha-512'); "
                        "return (a===b && a!==c && a.length>0) ? 'ok':'bad'; })()"),
                 "\"ok\"");

    // setTimeout fires.
    h.eval("globalThis.__t = 0; setTimeout(() => { globalThis.__t = 7; }, 5); 'scheduled'");
    std::this_thread::sleep_for(60ms);
    ok &= expect("setTimeout", h.eval("globalThis.__t"), "7");

    // clearTimeout cancels.
    h.eval("globalThis.__c = 0; const id = setTimeout(() => { globalThis.__c = 1; }, 40); clearTimeout(id); 'ok'");
    std::this_thread::sleep_for(80ms);
    ok &= expect("clearTimeout", h.eval("globalThis.__c"), "0");

    return ok ? 0 : 1;
}
