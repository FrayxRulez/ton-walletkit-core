//
// ton-walletkit-core — single-threaded event loop (internal).
//
#include "engine/event_loop.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace twk {

namespace {

// A timer that came due in this wakeup, pulled out so the batch can be ordered
// before anything runs.
struct DueTimer {
    std::chrono::steady_clock::time_point due;
    EventLoop::TimerId id;
    EventLoop::Task callback;
};

} // namespace

EventLoop::~EventLoop() {
    stop();
}

void EventLoop::start(Hooks hooks) {
    thread_ = std::thread(&EventLoop::run, this, std::move(hooks));
}

void EventLoop::stop() {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        stop_requested_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable() && std::this_thread::get_id() != thread_.get_id()) {
        thread_.join();
    }
}

bool EventLoop::isRunning() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return running_;
}

void EventLoop::post(Task task) {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (stop_requested_) {
            return;
        }
        tasks_.push_back(std::move(task));
    }
    cv_.notify_all();
}

EventLoop::TimerId EventLoop::addTimer(int64_t delay_ms, bool repeat, Task callback) {
    TimerId id;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        id = next_timer_id_++;
        timers_.push_back(Timer{
            id,
            std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms),
            delay_ms,
            repeat,
            std::move(callback),
        });
    }
    cv_.notify_all();
    return id;
}

void EventLoop::clearTimer(TimerId id) {
    std::lock_guard<std::mutex> guard(mutex_);
    for (size_t i = 0; i < timers_.size(); ++i) {
        if (timers_[i].id == id) {
            timers_.erase(timers_.begin() + i);
            return;
        }
    }
}

void EventLoop::run(Hooks hooks) {
    if (hooks.on_start) {
        hooks.on_start();
    }
    {
        std::lock_guard<std::mutex> guard(mutex_);
        running_ = true;
    }

    std::vector<Task> ready;
    std::vector<DueTimer> due_now;
    for (;;) {
        ready.clear();
        {
            std::unique_lock<std::mutex> lock(mutex_);
            for (;;) {
                if (stop_requested_) {
                    running_ = false;

                    // Tasks that were queued but never got to run can own JS values
                    // (the host completions capture QuickJS callbacks), and those may
                    // only be released on this thread while the runtime is still up.
                    // Destroying them here rather than leaving them to ~EventLoop,
                    // which runs on whichever thread destroys the owner — and runs
                    // after on_stop has already torn the runtime down.
                    std::deque<Task> abandoned;
                    abandoned.swap(tasks_);
                    lock.unlock();
                    abandoned.clear();

                    if (hooks.on_stop) {
                        hooks.on_stop();
                    }
                    return;
                }

                const auto now = std::chrono::steady_clock::now();
                std::optional<std::chrono::steady_clock::time_point> earliest;

                // Collect due timers; reschedule repeats, drop fired one-shots.
                due_now.clear();
                for (size_t i = 0; i < timers_.size();) {
                    Timer& timer = timers_[i];
                    if (timer.due <= now) {
                        due_now.push_back(DueTimer{timer.due, timer.id, timer.callback});
                        if (timer.repeat) {
                            timer.due = now + std::chrono::milliseconds(timer.period_ms);
                            ++i;
                        } else {
                            timers_.erase(timers_.begin() + i);
                        }
                    } else {
                        if (!earliest || timer.due < *earliest) {
                            earliest = timer.due;
                        }
                        ++i;
                    }
                }

                // By deadline, then by id so same-deadline timers keep FIFO order.
                // When the loop wakes late — a loaded machine, a sanitizer build —
                // several timers are due at once, and firing them in whatever order
                // they happen to sit in the vector would run a 60ms timer before a
                // 20ms one. Only sorted when it can matter: one timer is the norm.
                if (due_now.size() > 1) {
                    std::sort(due_now.begin(), due_now.end(), [](const DueTimer& a, const DueTimer& b) {
                        return a.due != b.due ? a.due < b.due : a.id < b.id;
                    });
                }
                for (DueTimer& timer : due_now) {
                    ready.push_back(std::move(timer.callback));
                }

                // Collect queued tasks.
                while (!tasks_.empty()) {
                    ready.push_back(std::move(tasks_.front()));
                    tasks_.pop_front();
                }

                if (!ready.empty()) {
                    break;
                }

                if (earliest) {
                    cv_.wait_until(lock, *earliest);
                } else {
                    cv_.wait(lock);
                }
            }
        }

        // Run callbacks outside the lock (they may post tasks / add timers).
        for (auto& task : ready) {
            if (task) {
                task();
            }
        }
        if (hooks.after_work) {
            hooks.after_work();
        }
    }
}

} // namespace twk
