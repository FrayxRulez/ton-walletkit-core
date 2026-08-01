//
// ton-walletkit-core — single-threaded event loop (internal).
//
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace twk {

// A worker-thread event loop: a task queue + timer scheduling + per-batch hook.
//
// Everything JS happens on this one thread. The owner (the client) creates the
// JsRuntime in `on_start`, drains QuickJS microtasks in `after_work` (run after
// each batch of tasks/timers), and tears the runtime down in `on_stop` — all on
// the worker thread. The loop itself knows nothing about JS.
class EventLoop {
public:
    using Task = std::function<void()>;
    using TimerId = uint64_t;

    struct Hooks {
        Task on_start;    // worker thread, once, before the loop begins
        Task after_work;  // worker thread, after each batch of tasks/timers
        Task on_stop;     // worker thread, once, after the loop ends
    };

    EventLoop() = default;
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // Launch the worker thread. Call once.
    void start(Hooks hooks);

    // Signal stop and join the worker thread. Idempotent; safe from any thread
    // except the worker thread itself.
    void stop();

    bool isRunning() const;

    // Enqueue a task to run on the worker thread. Thread-safe.
    //
    // After stop() the task is dropped — and therefore destroyed on the calling
    // thread. Anything a task owns that is worker-thread-only (JS values above
    // all) must not reach post() once the loop can be stopping; the client keeps
    // that true by leaving the live-client registry before it stops the loop, so
    // no host completion is still posting by then.
    void post(Task task);

    // Schedule a timer. `delay_ms` is both the initial delay and, when `repeat`,
    // the period. Returns an id usable with clearTimer. Thread-safe.
    TimerId addTimer(int64_t delay_ms, bool repeat, Task callback);

    // Cancel a timer by id (best-effort: a callback already dispatched still runs).
    void clearTimer(TimerId id);

private:
    void run(Hooks hooks);

    struct Timer {
        TimerId id;
        std::chrono::steady_clock::time_point due;
        int64_t period_ms;
        bool repeat;
        Task callback;
    };

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    bool running_ = false;
    bool stop_requested_ = false;
    std::deque<Task> tasks_;
    std::vector<Timer> timers_;
    TimerId next_timer_id_ = 1;
};

} // namespace twk
