// Reusable synchronization primitives for producer/consumer queues and
// one-background-thread lifecycle management.

export module cppx.sync;
import std;

export namespace cppx::sync {

template <class T>
class work_queue {
public:
    work_queue() = default;

    work_queue(work_queue const&) = delete;
    auto operator=(work_queue const&) -> work_queue& = delete;
    work_queue(work_queue&&) = delete;
    auto operator=(work_queue&&) -> work_queue& = delete;

    template <class U>
        requires std::constructible_from<T, U>
    auto push(U&& value) -> bool {
        auto lock = std::lock_guard{mutex_};
        if (closed_)
            return false;
        queue_.emplace_back(std::forward<U>(value));
        cv_.notify_one();
        return true;
    }

    auto try_pop() -> std::optional<T> {
        auto lock = std::lock_guard{mutex_};
        if (queue_.empty())
            return std::nullopt;

        auto value = std::optional<T>{
            std::move_if_noexcept(queue_.front())};
        queue_.pop_front();
        return value;
    }

    auto wait_pop() -> std::optional<T> {
        auto lock = std::unique_lock{mutex_};
        cv_.wait(lock, [this] {
            return closed_ || !queue_.empty();
        });
        if (queue_.empty())
            return std::nullopt;

        auto value = std::optional<T>{
            std::move_if_noexcept(queue_.front())};
        queue_.pop_front();
        return value;
    }

    void close() {
        {
            auto lock = std::lock_guard{mutex_};
            if (closed_)
                return;
            closed_ = true;
        }
        cv_.notify_all();
    }

    auto closed() const -> bool {
        auto lock = std::lock_guard{mutex_};
        return closed_;
    }

    auto empty() const -> bool {
        auto lock = std::lock_guard{mutex_};
        return queue_.empty();
    }

    auto size() const -> std::size_t {
        auto lock = std::lock_guard{mutex_};
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<T> queue_;
    bool closed_ = false;
};

template <class Key, class T>
    requires requires(Key const& key) {
        { key == key } -> std::convertible_to<bool>;
    }
class coalescing_queue {
public:
    struct entry {
        Key key;
        T value;
    };

    coalescing_queue() = default;

    coalescing_queue(coalescing_queue const&) = delete;
    auto operator=(coalescing_queue const&) -> coalescing_queue& = delete;
    coalescing_queue(coalescing_queue&&) = delete;
    auto operator=(coalescing_queue&&) -> coalescing_queue& = delete;

    template <class K, class U>
        requires std::constructible_from<Key, K>
              && std::constructible_from<T, U>
    auto push(K&& key, U&& value) -> bool {
        auto lock = std::lock_guard{mutex_};
        if (closed_)
            return false;

        auto queued_key = Key(std::forward<K>(key));
        auto duplicate = std::ranges::any_of(queue_, [&](entry const& queued) {
            return queued.key == queued_key;
        });
        if (duplicate)
            return false;

        queue_.push_back(entry{
            .key = std::move(queued_key),
            .value = T(std::forward<U>(value)),
        });

        cv_.notify_one();
        return true;
    }

    auto try_pop() -> std::optional<entry> {
        auto lock = std::lock_guard{mutex_};
        if (queue_.empty())
            return std::nullopt;

        auto next = std::optional<entry>{
            std::move_if_noexcept(queue_.front())};
        queue_.pop_front();
        return next;
    }

    auto wait_pop() -> std::optional<entry> {
        auto lock = std::unique_lock{mutex_};
        cv_.wait(lock, [this] {
            return closed_ || !queue_.empty();
        });
        if (queue_.empty())
            return std::nullopt;

        auto next = std::optional<entry>{
            std::move_if_noexcept(queue_.front())};
        queue_.pop_front();
        return next;
    }

    void close() {
        {
            auto lock = std::lock_guard{mutex_};
            if (closed_)
                return;
            closed_ = true;
        }
        cv_.notify_all();
    }

    auto closed() const -> bool {
        auto lock = std::lock_guard{mutex_};
        return closed_;
    }

    auto empty() const -> bool {
        auto lock = std::lock_guard{mutex_};
        return queue_.empty();
    }

    auto size() const -> std::size_t {
        auto lock = std::lock_guard{mutex_};
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<entry> queue_;
    bool closed_ = false;
};

class background_worker {
public:
    struct options {
        std::function<void()> on_stop;
        std::function<void(std::exception_ptr)> on_exception;
    };

    background_worker() = default;

    explicit background_worker(
        std::function<void(std::stop_token)> body,
        options opts = {})
        : state_{std::make_unique<state>(std::move(opts))}
    {
        if (!body) {
            body = [](std::stop_token) {};
        }

        auto* shared = state_.get();
#if defined(__APPLE__)
        shared->thread = std::thread{
            [shared, body = std::move(body)]() mutable {
                try {
                    body({});
                } catch (...) {
                    shared->report_failure(std::current_exception());
                }
            }};
#else
        shared->thread = std::jthread{
            [shared, body = std::move(body)](std::stop_token stop) mutable {
                try {
                    body(stop);
                } catch (...) {
                    shared->report_failure(std::current_exception());
                }
            }};
#endif
    }

    background_worker(background_worker const&) = delete;
    auto operator=(background_worker const&) -> background_worker& = delete;

    background_worker(background_worker&& other) noexcept = default;

    auto operator=(background_worker&& other) noexcept -> background_worker& {
        if (this == &other)
            return *this;
        close();
        state_ = std::move(other.state_);
        return *this;
    }

    ~background_worker() {
        close();
    }

    void request_stop() {
        if (!state_ || !state_->thread.joinable())
            return;

        state_->stop_requested.store(true);
#if !defined(__APPLE__)
        if (state_->thread.joinable())
            state_->thread.request_stop();
#endif
    }

    void join() {
        if (!state_ || !state_->thread.joinable())
            return;

        // Self-join would deadlock; detach in that rare case so teardown can
        // still complete without taking the process down.
        if (state_->thread.get_id() == std::this_thread::get_id()) {
            state_->thread.detach();
            return;
        }

        state_->thread.join();
    }

    void close() {
        if (!state_)
            return;

        if (!state_->close_started.exchange(true)) {
            state_->run_on_stop();
            request_stop();
        }

        join();
    }

    auto joinable() const -> bool {
        return state_ && state_->thread.joinable();
    }

    auto stop_requested() const -> bool {
        return state_ && state_->stop_requested.load();
    }

    auto exception() const -> std::exception_ptr {
        if (!state_)
            return nullptr;
        return state_->failure();
    }

    void rethrow_if_failed() const {
        if (auto failure = exception())
            std::rethrow_exception(failure);
    }

private:
    struct state {
        explicit state(options opts)
            : on_stop{std::move(opts.on_stop)},
              on_exception{std::move(opts.on_exception)} {}

        void run_on_stop() {
            if (!on_stop)
                return;
            try {
                on_stop();
            } catch (...) {
                report_failure(std::current_exception());
            }
        }

        void report_failure(std::exception_ptr failure_ptr) {
            auto callback = std::function<void(std::exception_ptr)>{};
            {
                auto lock = std::lock_guard{mutex};
                if (!failure_)
                    failure_ = failure_ptr;
                callback = on_exception;
            }

            if (!callback)
                return;

            try {
                callback(failure_ptr);
            } catch (...) {
                auto lock = std::lock_guard{mutex};
                if (!failure_)
                    failure_ = std::current_exception();
            }
        }

        auto failure() const -> std::exception_ptr {
            auto lock = std::lock_guard{mutex};
            return failure_;
        }

        mutable std::mutex mutex;
#if defined(__APPLE__)
        std::thread thread;
#else
        std::jthread thread;
#endif
        std::function<void()> on_stop;
        std::function<void(std::exception_ptr)> on_exception;
        std::exception_ptr failure_ = nullptr;
        std::atomic<bool> close_started = false;
        std::atomic<bool> stop_requested = false;
    };

    std::unique_ptr<state> state_;
};

} // namespace cppx::sync
