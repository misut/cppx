// Deterministic test executor for coroutines. Analogous to
// kotlinx-coroutine-test: virtual clock, advance_time_by,
// advance_until_idle. No real time, no real I/O — tests run
// instantly and repeatably.

export module cppx.async.test;
import cppx.async;
import std;

export namespace cppx::async::test {

// ---- delay — an awaitable that schedules at a future virtual time --------

// Forward declaration so delay can reference the executor.
class test_executor;

namespace detail {

// Per-coroutine storage: which executor it runs on.
// Set by the test_executor when it resumes a coroutine.
inline thread_local test_executor* current_executor = nullptr;

} // namespace detail

// ---- test_executor -------------------------------------------------------

class test_executor {
public:
    using clock = std::chrono::steady_clock;
    using duration = clock::duration;
    using time_point = clock::time_point;

    // Satisfies executor_engine.
    void schedule(std::coroutine_handle<> h) {
        queue_.push({current_time_, h});
    }

    // Schedule a coroutine to resume at a specific virtual time.
    void schedule_at(duration when, std::coroutine_handle<> h) {
        queue_.push({when, h});
    }

    // Run all immediately-ready coroutines (scheduled at or before
    // current_time_), then stop. Does NOT advance the clock.
    void run() {
        drain_ready();
    }

    // Advance virtual clock by `dt` and run all coroutines whose
    // scheduled time has been reached.
    void advance_time_by(duration dt) {
        auto target = current_time_ + dt;
        while (!queue_.empty() && queue_.top().when <= target) {
            current_time_ = queue_.top().when;
            auto h = queue_.top().handle;
            queue_.pop();
            resume(h);
        }
        current_time_ = target;
    }

    // Drain all enqueued coroutines regardless of their scheduled
    // time, advancing the clock as needed.
    void advance_until_idle() {
        while (!queue_.empty()) {
            auto const& top = queue_.top();
            if (top.when > current_time_)
                current_time_ = top.when;
            auto h = top.handle;
            queue_.pop();
            resume(h);
        }
    }

    // Check whether there are pending coroutines.
    auto has_pending() const -> bool { return !queue_.empty(); }

    // Current virtual time since epoch (start of test).
    auto current_time() const -> duration { return current_time_; }

    // Number of pending coroutines.
    auto pending_count() const -> std::size_t { return queue_.size(); }

private:
    struct entry {
        duration when;
        std::coroutine_handle<> handle;

        auto operator>(entry const& other) const -> bool {
            return when > other.when;
        }
    };

    void drain_ready() {
        while (!queue_.empty() && queue_.top().when <= current_time_) {
            auto h = queue_.top().handle;
            queue_.pop();
            resume(h);
        }
    }

    void resume(std::coroutine_handle<> h) {
        auto* prev = detail::current_executor;
        detail::current_executor = this;
        if (!h.done()) h.resume();
        detail::current_executor = prev;
    }

    duration current_time_{};
    std::priority_queue<entry, std::vector<entry>, std::greater<>> queue_;
};

static_assert(cppx::async::executor_engine<test_executor>);

// ---- delay — suspend for a virtual duration ------------------------------

class delay {
public:
    explicit delay(test_executor::duration dt) : dt_{dt} {}

    bool await_ready() const noexcept { return dt_ <= test_executor::duration::zero(); }

    void await_suspend(std::coroutine_handle<> h) const {
        auto* ex = detail::current_executor;
        if (ex)
            ex->schedule_at(ex->current_time() + dt_, h);
    }

    void await_resume() const noexcept {}

private:
    test_executor::duration dt_;
};

// ---- run_test — top-level test driver ------------------------------------

// Runs a coroutine-returning callable on a test_executor, advancing
// the clock until idle. Returns the task's result. If child coroutines
// remain pending after the root completes, the behavior is logged but
// not asserted (caller can check executor.has_pending()).

template <class F>
    requires std::invocable<F, test_executor&>
auto run_test(F&& fn) {
    test_executor executor;
    auto t = std::forward<F>(fn)(executor);
    executor.schedule(t.handle());
    executor.advance_until_idle();
    return t.result();
}

template <class F>
    requires std::invocable<F, test_executor&>
void run_test_void(F&& fn) {
    test_executor executor;
    auto t = std::forward<F>(fn)(executor);
    executor.schedule(t.handle());
    executor.advance_until_idle();
    t.result();
}

} // namespace cppx::async::test
