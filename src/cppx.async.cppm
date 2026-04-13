// Coroutine primitives. Pure library — no I/O, no platform headers.
// Provides task<T> (lazy awaitable), generator<T> (lazy sequence),
// and the executor_engine concept that abstracts coroutine scheduling.

export module cppx.async;
import std;

export namespace cppx::async {

// ---- forward declarations ------------------------------------------------

template <class T = void>
class task;

// ---- executor_engine concept ---------------------------------------------

// The seam between real and test schedulers. A real executor drives
// coroutines via an event loop (kqueue/epoll); a test executor uses
// a virtual clock so tests run deterministically.
template <class E>
concept executor_engine = requires(E& e, std::coroutine_handle<> h) {
    { e.schedule(h) } -> std::same_as<void>;
    { e.run() }       -> std::same_as<void>;
};

// ---- task<T> — lazy awaitable coroutine ----------------------------------

namespace detail {

struct final_awaiter {
    bool await_ready() noexcept { return false; }

    template <class P>
    auto await_suspend(std::coroutine_handle<P> h) noexcept
        -> std::coroutine_handle<> {
        if (auto cont = h.promise().continuation)
            return cont;
        return std::noop_coroutine();
    }

    void await_resume() noexcept {}
};

template <class T>
struct promise_base {
    std::coroutine_handle<> continuation = nullptr;

    auto initial_suspend() noexcept -> std::suspend_always { return {}; }
    auto final_suspend() noexcept -> final_awaiter { return {}; }

    void unhandled_exception() {
        result_.template emplace<2>(std::current_exception());
    }

    auto get_result() -> T {
        if (result_.index() == 2)
            std::rethrow_exception(std::get<2>(result_));
        return std::move(std::get<1>(result_));
    }

protected:
    std::variant<std::monostate, T, std::exception_ptr> result_;
};

template <>
struct promise_base<void> {
    std::coroutine_handle<> continuation = nullptr;

    auto initial_suspend() noexcept -> std::suspend_always { return {}; }
    auto final_suspend() noexcept -> final_awaiter { return {}; }

    void unhandled_exception() {
        exception_ = std::current_exception();
    }

    void get_result() {
        if (exception_)
            std::rethrow_exception(exception_);
    }

protected:
    std::exception_ptr exception_ = nullptr;
};

} // namespace detail

template <class T>
class task {
public:
    struct promise_type : detail::promise_base<T> {
        auto get_return_object() -> task {
            return task{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        void return_value(T value)
            requires(!std::is_void_v<T>)
        {
            this->result_.template emplace<1>(std::move(value));
        }
    };

    task(task const&) = delete;
    auto operator=(task const&) -> task& = delete;

    task(task&& other) noexcept
        : handle_{std::exchange(other.handle_, nullptr)} {}
    auto operator=(task&& other) noexcept -> task& {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    ~task() {
        if (handle_) handle_.destroy();
    }

    // Awaitable interface — allows `co_await some_task()`.
    auto operator co_await() & noexcept { return make_awaiter(); }
    auto operator co_await() && noexcept { return make_awaiter(); }

    // Low-level access for executors.
    auto handle() const noexcept -> std::coroutine_handle<promise_type> {
        return handle_;
    }
    auto done() const noexcept -> bool { return handle_.done(); }
    auto result() -> T { return handle_.promise().get_result(); }

private:
    explicit task(std::coroutine_handle<promise_type> h) : handle_{h} {}
    std::coroutine_handle<promise_type> handle_;

    struct awaiter {
        std::coroutine_handle<promise_type> h;

        bool await_ready() noexcept { return h.done(); }

        auto await_suspend(std::coroutine_handle<> caller) noexcept
            -> std::coroutine_handle<> {
            h.promise().continuation = caller;
            return h;
        }

        auto await_resume() -> T {
            return h.promise().get_result();
        }
    };

    auto make_awaiter() noexcept -> awaiter { return {handle_}; }
};

// void specialization.
template <>
class task<void> {
public:
    struct promise_type : detail::promise_base<void> {
        auto get_return_object() -> task {
            return task{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        void return_void() {}
    };

    task(task const&) = delete;
    auto operator=(task const&) -> task& = delete;

    task(task&& other) noexcept
        : handle_{std::exchange(other.handle_, nullptr)} {}
    auto operator=(task&& other) noexcept -> task& {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    ~task() {
        if (handle_) handle_.destroy();
    }

    auto operator co_await() & noexcept { return make_awaiter(); }
    auto operator co_await() && noexcept { return make_awaiter(); }

    auto handle() const noexcept -> std::coroutine_handle<promise_type> {
        return handle_;
    }
    auto done() const noexcept -> bool { return handle_.done(); }
    void result() { handle_.promise().get_result(); }

private:
    explicit task(std::coroutine_handle<promise_type> h) : handle_{h} {}
    std::coroutine_handle<promise_type> handle_;

    struct awaiter {
        std::coroutine_handle<promise_type> h;

        bool await_ready() noexcept { return h.done(); }

        auto await_suspend(std::coroutine_handle<> caller) noexcept
            -> std::coroutine_handle<> {
            h.promise().continuation = caller;
            return h;
        }

        void await_resume() {
            h.promise().get_result();
        }
    };

    auto make_awaiter() noexcept -> awaiter { return {handle_}; }
};

// ---- run — drive a task to completion on an executor ---------------------

template <executor_engine E, class T>
auto run(E& executor, task<T>& t) -> T {
    executor.schedule(t.handle());
    executor.run();
    return t.result();
}

template <executor_engine E>
void run(E& executor, task<void>& t) {
    executor.schedule(t.handle());
    executor.run();
    t.result();
}

// ---- generator<T> — lazy pull-based sequence -----------------------------

template <class T>
class generator {
public:
    struct promise_type {
        T const* current_ = nullptr;

        auto get_return_object() -> generator {
            return generator{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        auto initial_suspend() noexcept -> std::suspend_always { return {}; }
        auto final_suspend() noexcept -> std::suspend_always { return {}; }
        void unhandled_exception() { std::rethrow_exception(std::current_exception()); }
        void return_void() {}

        auto yield_value(T const& value) noexcept -> std::suspend_always {
            current_ = std::addressof(value);
            return {};
        }

        auto yield_value(T&& value) noexcept -> std::suspend_always {
            current_ = std::addressof(value);
            return {};
        }
    };

    generator(generator const&) = delete;
    auto operator=(generator const&) -> generator& = delete;

    generator(generator&& other) noexcept
        : handle_{std::exchange(other.handle_, nullptr)} {}
    auto operator=(generator&& other) noexcept -> generator& {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    ~generator() {
        if (handle_) handle_.destroy();
    }

    class iterator {
    public:
        using difference_type = std::ptrdiff_t;
        using value_type = T;

        iterator() = default;
        explicit iterator(std::coroutine_handle<promise_type> h) : handle_{h} {}

        auto operator++() -> iterator& {
            handle_.resume();
            if (handle_.done()) handle_ = nullptr;
            return *this;
        }
        void operator++(int) { ++*this; }

        auto operator*() const -> T const& { return *handle_.promise().current_; }

        auto operator==(std::default_sentinel_t) const -> bool {
            return !handle_ || handle_.done();
        }

    private:
        std::coroutine_handle<promise_type> handle_ = nullptr;
    };

    auto begin() -> iterator {
        if (handle_) {
            handle_.resume();
            if (handle_.done()) return iterator{};
        }
        return iterator{handle_};
    }

    auto end() -> std::default_sentinel_t { return {}; }

private:
    explicit generator(std::coroutine_handle<promise_type> h) : handle_{h} {}
    std::coroutine_handle<promise_type> handle_;
};

// ---- inline_executor — minimal executor for testing & sync bridges -------

// Runs coroutines inline on the calling thread. No event loop, no
// virtual clock — just a queue drained synchronously.
class inline_executor {
public:
    void schedule(std::coroutine_handle<> h) { queue_.push_back(h); }

    void run() {
        while (!queue_.empty()) {
            auto h = queue_.front();
            queue_.pop_front();
            if (!h.done()) h.resume();
        }
    }

private:
    std::deque<std::coroutine_handle<>> queue_;
};

static_assert(executor_engine<inline_executor>);

// ---- structured concurrency primitives -----------------------------------

// async_scope owns a set of spawned child tasks. All children must
// complete before the scope is destroyed. co_await scope.join()
// suspends until every spawned task finishes.
class async_scope {
public:
    async_scope() = default;
    ~async_scope() = default;

    async_scope(async_scope const&) = delete;
    auto operator=(async_scope const&) -> async_scope& = delete;
    async_scope(async_scope&&) = delete;
    auto operator=(async_scope&&) -> async_scope& = delete;

    // Spawn a task<void> into this scope. The task starts running
    // when the executor next drains its ready queue.
    void spawn(task<void>&& t) {
        auto wrapper = [this](task<void> inner) -> task<void> {
            co_await std::move(inner);
            --pending_;
            if (pending_ == 0 && waiter_)
                waiter_.resume();
        };
        ++pending_;
        auto w = wrapper(std::move(t));
        // Start the wrapper — resume past initial suspend so it can
        // be scheduled on whatever executor the caller is using.
        w.handle().resume();
        // Keep the wrapper alive until it completes.
        tasks_.push_back(std::move(w));
    }

    // Suspend until all spawned tasks have completed.
    auto join() {
        struct join_awaiter {
            async_scope& scope;

            bool await_ready() const noexcept {
                return scope.pending_ == 0;
            }
            void await_suspend(std::coroutine_handle<> h) noexcept {
                scope.waiter_ = h;
            }
            void await_resume() const noexcept {}
        };
        return join_awaiter{*this};
    }

    auto pending() const -> int { return pending_; }

private:
    int pending_ = 0;
    std::coroutine_handle<> waiter_ = nullptr;
    std::vector<task<void>> tasks_;
};

// when_all — run multiple tasks concurrently, return when all complete.

namespace detail {

template <class T>
auto when_all_impl(std::vector<task<T>>& tasks) -> task<std::vector<T>> {
    auto results = std::vector<T>{};
    results.reserve(tasks.size());
    for (auto& t : tasks)
        results.push_back(co_await t);
    co_return results;
}

inline auto when_all_void_impl(std::vector<task<void>>& tasks) -> task<void> {
    for (auto& t : tasks)
        co_await t;
}

} // namespace detail

template <class T>
auto when_all(std::vector<task<T>>& tasks) -> task<std::vector<T>> {
    return detail::when_all_impl(tasks);
}

inline auto when_all(std::vector<task<void>>& tasks) -> task<void> {
    return detail::when_all_void_impl(tasks);
}

} // namespace cppx::async
