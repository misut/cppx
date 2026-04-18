// Tests for cppx.async.test — deterministic executor, virtual time,
// stable scheduling, and external wake-up hooks.

import cppx.async;
import cppx.async.test;
import cppx.test;
import std;

cppx::test::context tc;
using namespace std::chrono_literals;

// ---- basic executor usage ------------------------------------------------

cppx::async::task<void> set_flag(bool& flag) {
    flag = true;
    co_return;
}

void test_executor_schedule_and_run() {
    cppx::async::test::test_executor ex;
    bool ran = false;
    auto coro = set_flag(ran);
    ex.schedule(coro.handle());
    tc.check(!ran, "coroutine not started before run");
    ex.run();
    coro.result();
    tc.check(ran, "coroutine ran after run()");
}

// ---- delay with advance_time_by ------------------------------------------

cppx::async::task<void> step_sequence(int& step) {
    step = 1;
    co_await cppx::async::test::delay{100ms};
    step = 2;
    co_await cppx::async::test::delay{200ms};
    step = 3;
}

void test_delay_advance_time_by() {
    cppx::async::test::test_executor ex;
    int step = 0;
    auto coro = step_sequence(step);

    ex.schedule(coro.handle());
    ex.run();
    tc.check_eq(step, 1, "step 1 after initial run");

    ex.advance_time_by(50ms);
    tc.check_eq(step, 1, "still step 1 at 50ms");

    ex.advance_time_by(50ms);
    tc.check_eq(step, 2, "step 2 at 100ms");

    ex.advance_time_by(100ms);
    tc.check_eq(step, 2, "still step 2 at 200ms");

    ex.advance_time_by(100ms);
    coro.result();
    tc.check_eq(step, 3, "step 3 at 300ms");
}

// ---- advance_until_idle --------------------------------------------------

cppx::async::task<void> two_stage_delay(int& completed) {
    co_await cppx::async::test::delay{500ms};
    ++completed;
    co_await cppx::async::test::delay{1s};
    ++completed;
}

void test_advance_until_idle() {
    cppx::async::test::test_executor ex;
    int completed = 0;
    auto coro = two_stage_delay(completed);

    ex.schedule(coro.handle());
    ex.advance_until_idle();
    coro.result();

    tc.check_eq(completed, 2, "all stages completed");
    tc.check(!ex.has_pending(), "no pending coroutines");
    tc.check(ex.current_time() == 1500ms, "virtual clock advanced to 1500ms");
}

// ---- stable FIFO ordering at the same virtual time -----------------------

cppx::async::task<void> append_after(
    std::vector<std::string>& log,
    std::string message) {
    co_await cppx::async::test::delay{100ms};
    log.push_back(std::move(message));
}

void test_same_timestamp_fifo() {
    cppx::async::test::test_executor ex;
    std::vector<std::string> log;

    auto first = append_after(log, "first");
    auto second = append_after(log, "second");

    ex.schedule(first.handle());
    ex.schedule(second.handle());
    ex.advance_until_idle();
    first.result();
    second.result();

    tc.check_eq(static_cast<int>(log.size()), 2, "both delayed tasks completed");
    tc.check_eq(log[0], std::string{"first"}, "same timestamp keeps FIFO order");
    tc.check_eq(log[1], std::string{"second"}, "second task follows first");
}

// ---- external schedule-based wake-up -------------------------------------

struct manual_signal {
    cppx::async::test::test_executor* executor = nullptr;
    std::coroutine_handle<> waiter = nullptr;
    bool ready = false;

    struct awaiter {
        manual_signal& signal;

        auto await_ready() const noexcept -> bool {
            return signal.ready;
        }

        void await_suspend(std::coroutine_handle<> handle) const {
            signal.executor = cppx::async::test::current_executor();
            signal.waiter = handle;
        }

        void await_resume() const noexcept {}
    };

    auto wait() -> awaiter {
        return awaiter{*this};
    }

    void fire() {
        ready = true;
        if (executor && waiter) {
            executor->schedule(waiter);
            waiter = nullptr;
        }
    }
};

cppx::async::task<void> wait_for_signal(manual_signal& signal, bool& done) {
    co_await signal.wait();
    done = true;
}

void test_external_schedule_wakeup() {
    cppx::async::test::test_executor ex;
    manual_signal signal;
    bool done = false;
    auto coro = wait_for_signal(signal, done);

    ex.schedule(coro.handle());
    ex.run();
    tc.check(!done, "coroutine suspended before signal fire");
    tc.check(signal.executor == &ex, "signal captured the current executor");

    signal.fire();
    ex.run();
    coro.result();
    tc.check(done, "external schedule resumes the waiter");
}

// ---- current_time tracking -----------------------------------------------

void test_current_time() {
    cppx::async::test::test_executor ex;
    tc.check(ex.current_time() == 0ms, "starts at 0");

    ex.advance_time_by(250ms);
    tc.check(ex.current_time() == 250ms, "at 250ms after advance");

    ex.advance_time_by(750ms);
    tc.check(ex.current_time() == 1s, "at 1s after second advance");
}

// ---- zero-delay resumes immediately --------------------------------------

cppx::async::task<void> zero_delay_coro(bool& done) {
    co_await cppx::async::test::delay{0ms};
    done = true;
}

void test_zero_delay() {
    cppx::async::test::test_executor ex;
    bool done = false;
    auto coro = zero_delay_coro(done);
    ex.schedule(coro.handle());
    ex.run();
    coro.result();
    tc.check(done, "zero delay resumes in the same run()");
}

// ---- run_test helper -----------------------------------------------------

cppx::async::task<int> return_42_coro(
    [[maybe_unused]] cppx::async::test::test_executor& ex) {
    co_return 42;
}

void test_run_test_returns_value() {
    auto val = cppx::async::test::run_test(return_42_coro);
    tc.check_eq(val, 42, "run_test returns task value");
}

cppx::async::task<int> delayed_99(
    [[maybe_unused]] cppx::async::test::test_executor& ex) {
    co_await cppx::async::test::delay{5s};
    co_return 99;
}

void test_run_test_with_delay() {
    auto val = cppx::async::test::run_test(delayed_99);
    tc.check_eq(val, 99, "run_test advances through delays");
}

cppx::async::task<void> delayed_flag(
    [[maybe_unused]] cppx::async::test::test_executor& ex,
    bool& done) {
    co_await cppx::async::test::delay{1s};
    done = true;
}

void test_run_test_void() {
    bool done = false;
    cppx::async::test::run_test_void(
        [&](cppx::async::test::test_executor& ex)
            -> cppx::async::task<void> {
            return delayed_flag(ex, done);
        });
    tc.check(done, "run_test_void completes");
}

// ---- task chaining with delay --------------------------------------------

cppx::async::task<int> inner_with_delay() {
    co_await cppx::async::test::delay{100ms};
    co_return 10;
}

cppx::async::task<int> chained_with_delays(
    [[maybe_unused]] cppx::async::test::test_executor& ex) {
    auto inner = inner_with_delay();
    auto result = co_await inner;
    co_await cppx::async::test::delay{200ms};
    co_return result + 5;
}

void test_chained_tasks_with_delay() {
    auto val = cppx::async::test::run_test(chained_with_delays);
    tc.check_eq(val, 15, "chained tasks with delays: 10 + 5");
}

// ---- main ----------------------------------------------------------------

int main() {
    test_executor_schedule_and_run();
    test_delay_advance_time_by();
    test_advance_until_idle();
    test_same_timestamp_fifo();
    test_external_schedule_wakeup();
    test_current_time();
    test_zero_delay();
    test_run_test_returns_value();
    test_run_test_with_delay();
    test_run_test_void();
    test_chained_tasks_with_delay();
    return tc.summary("cppx.async.test");
}
