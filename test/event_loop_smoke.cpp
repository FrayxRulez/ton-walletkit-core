// Sanity checks for the EventLoop: task dispatch, timer ordering + cancellation,
// and the after_work hook draining QuickJS microtasks in order. Returns 0 on pass.
#include <chrono>
#include <cstdio>
#include <future>
#include <mutex>
#include <string>
#include <vector>

#include "engine/event_loop.h"
#include "engine/js_runtime.h"

using namespace twk;
using namespace std::chrono_literals;

static bool test_tasks() {
    EventLoop loop;
    loop.start({});

    std::promise<int> done;
    auto fut = done.get_future();
    std::atomic<int> counter{0};
    for (int i = 0; i < 5; ++i) {
        loop.post([&counter] { counter.fetch_add(1); });
    }
    loop.post([&] { done.set_value(counter.load()); });

    int seen = fut.get();
    loop.stop();
    if (seen != 5) {
        printf("test_tasks: expected 5, got %d\n", seen);
        return false;
    }
    return true;
}

static bool test_timers() {
    EventLoop loop;
    loop.start({});

    std::mutex m;
    std::vector<int> order;
    auto record = [&](int v) { return [&, v] { std::lock_guard<std::mutex> g(m); order.push_back(v); }; };

    loop.addTimer(60, false, record(60));
    loop.addTimer(20, false, record(20));
    loop.addTimer(40, false, record(40));
    auto cancelled = loop.addTimer(30, false, record(999));
    loop.clearTimer(cancelled);

    std::this_thread::sleep_for(160ms);
    loop.stop();

    std::lock_guard<std::mutex> g(m);
    std::vector<int> expected{20, 40, 60};
    if (order != expected) {
        printf("test_timers: unexpected order (size %zu)\n", order.size());
        return false;
    }
    return true;
}

static bool test_repeat_timer() {
    EventLoop loop;
    loop.start({});

    std::atomic<int> ticks{0};
    auto id = loop.addTimer(15, /*repeat=*/true, [&] { ticks.fetch_add(1); });
    std::this_thread::sleep_for(100ms);
    loop.clearTimer(id);
    int after_clear = ticks.load();
    std::this_thread::sleep_for(60ms);
    loop.stop();

    if (after_clear < 3) {
        printf("test_repeat_timer: too few ticks (%d)\n", after_clear);
        return false;
    }
    if (ticks.load() != after_clear) {
        printf("test_repeat_timer: kept ticking after clear (%d -> %d)\n", after_clear, ticks.load());
        return false;
    }
    return true;
}

// The after_work hook drains QuickJS microtasks between task batches, so a Promise
// scheduled in one task has resolved before the next task reads its effect.
static bool test_js_microtask_pump() {
    JsRuntime* js = nullptr;
    EventLoop loop;

    EventLoop::Hooks hooks;
    hooks.on_start = [&] { js = new JsRuntime(); };
    hooks.after_work = [&] {
        if (js) {
            JSContext* c = nullptr;
            while (JS_ExecutePendingJob(js->runtime(), &c) > 0) {
            }
        }
    };
    hooks.on_stop = [&] {
        delete js;
        js = nullptr;
    };
    loop.start(std::move(hooks));

    std::promise<std::string> result;
    auto fut = result.get_future();

    loop.post([&] {
        std::string err;
        js->eval("globalThis.x = 0; Promise.resolve().then(() => { globalThis.x = 42; });", "<t>", nullptr, &err);
        // Posted from within this task -> runs in the NEXT batch, i.e. after the
        // after_work pump has resolved the promise above.
        loop.post([&] {
            std::string r, e;
            js->eval("globalThis.x", "<t>", &r, &e);
            result.set_value(r);
        });
    });

    std::string value = fut.get();
    loop.stop();
    if (value != "42") {
        printf("test_js_microtask_pump: expected 42, got '%s'\n", value.c_str());
        return false;
    }
    return true;
}

int main() {
    struct {
        const char* name;
        bool (*fn)();
    } tests[] = {
        {"tasks", test_tasks},
        {"timers", test_timers},
        {"repeat_timer", test_repeat_timer},
        {"js_microtask_pump", test_js_microtask_pump},
    };

    for (auto& t : tests) {
        if (!t.fn()) {
            printf("FAIL: %s\n", t.name);
            return 1;
        }
        printf("ok: %s\n", t.name);
    }
    return 0;
}
