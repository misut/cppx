// Tests for cppx.async — coroutine primitives and deterministic task driving.

import cppx.async;
import cppx.async.test;
import cppx.test;
import std;

cppx::test::context tc;

template <class F>
auto run_on_test_executor(F&& fn) {
    return cppx::async::test::run_test(
        [fn = std::forward<F>(fn)](cppx::async::test::test_executor&) mutable
            -> decltype(fn()) {
            return fn();
        });
}

template <class F>
void run_on_test_executor_void(F&& fn) {
    cppx::async::test::run_test_void(
        [fn = std::forward<F>(fn)](cppx::async::test::test_executor&) mutable
            -> decltype(fn()) {
            return fn();
        });
}

// ---- task<T> basics ------------------------------------------------------

cppx::async::task<int> return_42() {
    co_return 42;
}

cppx::async::task<std::string> return_hello() {
    co_return "hello";
}

cppx::async::task<void> do_nothing() {
    co_return;
}

void test_task_int() {
    auto val = run_on_test_executor(return_42);
    tc.check_eq(val, 42, "task<int> returns 42");
}

void test_task_string() {
    auto val = run_on_test_executor(return_hello);
    tc.check_eq(val, std::string{"hello"}, "task<string> returns hello");
}

void test_task_void() {
    run_on_test_executor_void(do_nothing);
    tc.check(true, "task<void> completes");
}

// ---- task chaining (co_await task) ---------------------------------------

cppx::async::task<int> add_one(int x) {
    co_return x + 1;
}

cppx::async::task<int> chain_tasks() {
    auto a = add_one(10);
    auto val = co_await a;
    auto b = add_one(val);
    co_return co_await b;
}

void test_task_chaining() {
    auto val = run_on_test_executor(chain_tasks);
    tc.check_eq(val, 12, "chained tasks: 10 + 1 + 1 = 12");
}

// ---- task exception propagation ------------------------------------------

cppx::async::task<int> throw_task() {
    throw std::runtime_error{"task error"};
    co_return 0;
}

cppx::async::task<int> catch_inner_exception() {
    try {
        auto t = throw_task();
        co_return co_await t;
    } catch (std::runtime_error const&) {
        co_return -1;
    }
}

void test_task_exception_propagation() {
    auto val = run_on_test_executor(catch_inner_exception);
    tc.check_eq(val, -1, "exception propagated through co_await");
}

void test_task_exception_at_top_level() {
    bool caught = false;
    try {
        (void)run_on_test_executor(throw_task);
    } catch (std::runtime_error const& e) {
        caught = true;
        tc.check_eq(std::string_view{e.what()},
                    std::string_view{"task error"},
                    "exception message preserved");
    }
    tc.check(caught, "exception thrown from run_test()");
}

// ---- task move semantics -------------------------------------------------

cppx::async::task<int> move_task_result() {
    auto t1 = return_42();
    auto t2 = std::move(t1);
    co_return co_await t2;
}

void test_task_move() {
    auto val = run_on_test_executor(move_task_result);
    tc.check_eq(val, 42, "moved task still works");
}

// ---- generator -----------------------------------------------------------

cppx::async::generator<int> iota(int start, int end) {
    for (int i = start; i < end; ++i)
        co_yield i;
}

void test_generator_basic() {
    auto gen = iota(0, 5);
    auto values = std::vector<int>{};
    for (auto v : gen)
        values.push_back(v);
    tc.check_eq(static_cast<int>(values.size()), 5, "generator yields 5 values");
    tc.check_eq(values[0], 0, "first value is 0");
    tc.check_eq(values[4], 4, "last value is 4");
}

cppx::async::generator<int> empty_gen() {
    co_return;
}

void test_generator_empty() {
    auto gen = empty_gen();
    int count = 0;
    for ([[maybe_unused]] auto v : gen)
        ++count;
    tc.check_eq(count, 0, "empty generator yields nothing");
}

cppx::async::generator<std::string> string_gen() {
    co_yield "alpha";
    co_yield "beta";
}

void test_generator_strings() {
    auto gen = string_gen();
    auto values = std::vector<std::string>{};
    for (auto const& v : gen)
        values.push_back(v);
    tc.check_eq(static_cast<int>(values.size()), 2, "string generator yields 2");
    tc.check_eq(values[0], std::string{"alpha"}, "first string");
    tc.check_eq(values[1], std::string{"beta"}, "second string");
}

// ---- executor_engine concept check ---------------------------------------

void test_concept_satisfaction() {
    static_assert(cppx::async::executor_engine<cppx::async::inline_executor>);
    static_assert(
        cppx::async::executor_engine<cppx::async::test::test_executor>);
    tc.check(true, "inline_executor and test_executor satisfy executor_engine");
}

// ---- async_scope ---------------------------------------------------------

cppx::async::task<void> increment(int& counter) {
    ++counter;
    co_return;
}

cppx::async::task<void> scope_test_coro(int& counter) {
    cppx::async::async_scope scope;
    scope.spawn(increment(counter));
    scope.spawn(increment(counter));
    scope.spawn(increment(counter));
    co_await scope.join();
}

void test_async_scope() {
    int counter = 0;
    run_on_test_executor_void([&]() {
        return scope_test_coro(counter);
    });
    tc.check_eq(counter, 3, "async_scope: 3 spawned tasks completed");
}

void test_async_scope_empty() {
    run_on_test_executor_void([]() -> cppx::async::task<void> {
        cppx::async::async_scope scope;
        co_await scope.join();
    });
    tc.check(true, "async_scope: empty join completes");
}

// ---- when_all ------------------------------------------------------------

cppx::async::task<int> return_n(int n) {
    co_return n;
}

cppx::async::task<std::vector<int>> collect_values() {
    auto tasks = std::vector<cppx::async::task<int>>{};
    tasks.push_back(return_n(1));
    tasks.push_back(return_n(2));
    tasks.push_back(return_n(3));
    co_return co_await cppx::async::when_all(tasks);
}

void test_when_all_values() {
    auto vals = run_on_test_executor(collect_values);
    tc.check_eq(static_cast<int>(vals.size()), 3, "when_all: 3 results");
    tc.check_eq(vals[0], 1, "when_all: first");
    tc.check_eq(vals[1], 2, "when_all: second");
    tc.check_eq(vals[2], 3, "when_all: third");
}

cppx::async::task<void> collect_voids(int& counter) {
    auto tasks = std::vector<cppx::async::task<void>>{};
    tasks.push_back(increment(counter));
    tasks.push_back(increment(counter));
    co_await cppx::async::when_all(tasks);
}

void test_when_all_void() {
    int counter = 0;
    run_on_test_executor_void([&]() {
        return collect_voids(counter);
    });
    tc.check_eq(counter, 2, "when_all void: 2 tasks completed");
}

// ---- main ----------------------------------------------------------------

int main() {
    test_task_int();
    test_task_string();
    test_task_void();
    test_task_chaining();
    test_task_exception_propagation();
    test_task_exception_at_top_level();
    test_task_move();
    test_generator_basic();
    test_generator_empty();
    test_generator_strings();
    test_concept_satisfaction();
    test_async_scope();
    test_async_scope_empty();
    test_when_all_values();
    test_when_all_void();
    return tc.summary("cppx.async");
}
