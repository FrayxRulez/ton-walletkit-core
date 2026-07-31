//
// ton-walletkit-core — single-threaded event loop (internal).
//
#include "engine/event_loop.h"

#include <optional>
#include <utility>

namespace twk {

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
    for (;;) {
        ready.clear();
        {
            std::unique_lock<std::mutex> lock(mutex_);
            for (;;) {
                if (stop_requested_) {
                    running_ = false;
                    lock.unlock();
                    if (hooks.on_stop) {
                        hooks.on_stop();
                    }
                    return;
                }

                const auto now = std::chrono::steady_clock::now();
                std::optional<std::chrono::steady_clock::time_point> earliest;

                // Collect due timers; reschedule repeats, drop fired one-shots.
                for (size_t i = 0; i < timers_.size();) {
                    Timer& timer = timers_[i];
                    if (timer.due <= now) {
                        ready.push_back(timer.callback);
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
