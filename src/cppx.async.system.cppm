// Platform event loop and async I/O. This is the impure edge for
// cppx.async — it wraps kqueue (macOS), epoll (Linux), or a fallback
// poll loop behind the executor_engine concept.
//
// On wasm32-wasi the module compiles as an empty stub.

module;

#if defined(__APPLE__)
#include <sys/event.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#endif

#if defined(__linux__)
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#endif

export module cppx.async.system;

#if !defined(__wasi__) && !defined(_WIN32)
import std;
import cppx.async;
import cppx.http;

export namespace cppx::async::system {

// ---- event_loop ----------------------------------------------------------

// Single-threaded event loop backed by kqueue (macOS) or epoll (Linux).
// Satisfies executor_engine so it can drive task<T> coroutines.

class event_loop {
public:
    event_loop() {
#if defined(__APPLE__)
        fd_ = ::kqueue();
#elif defined(__linux__)
        fd_ = ::epoll_create1(0);
#endif
        // Create a pipe for cross-thread wakeup and ready-queue drain.
        int fds[2];
        ::pipe(fds);
        wake_read_ = fds[0];
        wake_write_ = fds[1];
        set_nonblocking(wake_read_);
        set_nonblocking(wake_write_);

#if defined(__APPLE__)
        struct kevent ev;
        EV_SET(&ev, wake_read_, EVFILT_READ, EV_ADD, 0, 0, nullptr);
        ::kevent(fd_, &ev, 1, nullptr, 0, nullptr);
#elif defined(__linux__)
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = wake_read_;
        ::epoll_ctl(fd_, EPOLL_CTL_ADD, wake_read_, &ev);
#endif
    }

    ~event_loop() {
        if (fd_ >= 0) ::close(fd_);
        if (wake_read_ >= 0) ::close(wake_read_);
        if (wake_write_ >= 0) ::close(wake_write_);
    }

    event_loop(event_loop const&) = delete;
    auto operator=(event_loop const&) -> event_loop& = delete;

    // executor_engine interface.
    void schedule(std::coroutine_handle<> h) {
        ready_.push_back(h);
    }

    void run() {
        running_ = true;
        while (running_) {
            // Drain ready queue first.
            while (!ready_.empty()) {
                auto h = ready_.front();
                ready_.pop_front();
                if (!h.done()) h.resume();
            }

            if (!running_) break;
            if (ready_.empty() && io_count_ == 0 && timers_.empty()) break;

            wait_for_events();
        }
    }

    void stop() { running_ = false; wakeup(); }

    // ---- I/O registration (for async stream engines) --------------------

    // Register interest in a fd becoming readable. When ready, the
    // coroutine handle is scheduled for resumption.
    void watch_readable(int fd, std::coroutine_handle<> h) {
        ++io_count_;
#if defined(__APPLE__)
        struct kevent ev;
        EV_SET(&ev, fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0,
               reinterpret_cast<void*>(h.address()));
        ::kevent(fd_, &ev, 1, nullptr, 0, nullptr);
#elif defined(__linux__)
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLONESHOT;
        ev.data.ptr = h.address();
        if (::epoll_ctl(fd_, EPOLL_CTL_MOD, fd, &ev) < 0)
            ::epoll_ctl(fd_, EPOLL_CTL_ADD, fd, &ev);
#endif
    }

    void watch_writable(int fd, std::coroutine_handle<> h) {
        ++io_count_;
#if defined(__APPLE__)
        struct kevent ev;
        EV_SET(&ev, fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0,
               reinterpret_cast<void*>(h.address()));
        ::kevent(fd_, &ev, 1, nullptr, 0, nullptr);
#elif defined(__linux__)
        struct epoll_event ev;
        ev.events = EPOLLOUT | EPOLLONESHOT;
        ev.data.ptr = h.address();
        if (::epoll_ctl(fd_, EPOLL_CTL_MOD, fd, &ev) < 0)
            ::epoll_ctl(fd_, EPOLL_CTL_ADD, fd, &ev);
#endif
    }

    // ---- timer registration ---------------------------------------------

    void schedule_after(std::chrono::steady_clock::duration dt,
                        std::coroutine_handle<> h) {
        auto deadline = std::chrono::steady_clock::now() + dt;
        timers_.push({deadline, h});
        wakeup();
    }

private:
    struct timer_entry {
        std::chrono::steady_clock::time_point when;
        std::coroutine_handle<> handle;
        auto operator>(timer_entry const& o) const { return when > o.when; }
    };

    void set_nonblocking(int fd) {
        int flags = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    void wakeup() {
        char c = 1;
        [[maybe_unused]] auto _ = ::write(wake_write_, &c, 1);
    }

    void drain_wakeup_pipe() {
        char buf[64];
        while (::read(wake_read_, buf, sizeof(buf)) > 0) {}
    }

    auto compute_timeout_ms() -> int {
        if (!ready_.empty()) return 0;
        if (timers_.empty()) return 500; // idle poll interval
        auto now = std::chrono::steady_clock::now();
        auto dt = timers_.top().when - now;
        if (dt <= std::chrono::steady_clock::duration::zero()) return 0;
        return static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(dt).count());
    }

    void fire_expired_timers() {
        auto now = std::chrono::steady_clock::now();
        while (!timers_.empty() && timers_.top().when <= now) {
            ready_.push_back(timers_.top().handle);
            timers_.pop();
        }
    }

    void wait_for_events() {
        fire_expired_timers();
        if (!ready_.empty()) return;

        auto timeout = compute_timeout_ms();

#if defined(__APPLE__)
        struct kevent events[64];
        struct timespec ts;
        ts.tv_sec = timeout / 1000;
        ts.tv_nsec = (timeout % 1000) * 1000000L;
        int n = ::kevent(fd_, nullptr, 0, events, 64, &ts);
        for (int i = 0; i < n; ++i) {
            if (static_cast<int>(events[i].ident) == wake_read_) {
                drain_wakeup_pipe();
            } else if (events[i].udata) {
                --io_count_;
                ready_.push_back(std::coroutine_handle<>::from_address(
                    events[i].udata));
            }
        }
#elif defined(__linux__)
        struct epoll_event events[64];
        int n = ::epoll_wait(fd_, events, 64, timeout);
        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == wake_read_) {
                drain_wakeup_pipe();
            } else if (events[i].data.ptr) {
                --io_count_;
                ready_.push_back(std::coroutine_handle<>::from_address(
                    events[i].data.ptr));
            }
        }
#endif
        fire_expired_timers();
    }

    int fd_ = -1;
    int wake_read_ = -1;
    int wake_write_ = -1;
    bool running_ = false;
    int io_count_ = 0;
    std::deque<std::coroutine_handle<>> ready_;
    std::priority_queue<timer_entry, std::vector<timer_entry>,
                        std::greater<>> timers_;
};

static_assert(cppx::async::executor_engine<event_loop>);

// ---- async POSIX stream --------------------------------------------------

// Per-coroutine storage for the event loop.
inline thread_local event_loop* current_loop = nullptr;

class async_stream {
public:
    static auto connect(std::string_view host, std::uint16_t port)
        -> cppx::async::task<std::expected<async_stream, cppx::http::net_error>> {
        // Resolve address.
        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        auto port_str = std::to_string(port);
        auto host_str = std::string{host};
        struct addrinfo* res = nullptr;
        if (::getaddrinfo(host_str.c_str(), port_str.c_str(),
                          &hints, &res) != 0 || !res) {
            co_return std::unexpected(cppx::http::net_error::resolve_failed);
        }

        int fd = ::socket(res->ai_family, res->ai_socktype,
                          res->ai_protocol);
        if (fd < 0) {
            ::freeaddrinfo(res);
            co_return std::unexpected(cppx::http::net_error::connect_refused);
        }

        // Set non-blocking for async connect.
        int flags = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        int rc = ::connect(fd, res->ai_addr, res->ai_addrlen);
        ::freeaddrinfo(res);

        if (rc < 0 && errno == EINPROGRESS) {
            // Wait for writable (connect completion).
            co_await wait_writable(fd);

            int err = 0;
            socklen_t len = sizeof(err);
            ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err != 0) {
                ::close(fd);
                co_return std::unexpected(
                    cppx::http::net_error::connect_refused);
            }
        } else if (rc < 0) {
            ::close(fd);
            co_return std::unexpected(cppx::http::net_error::connect_refused);
        }

        co_return async_stream{fd};
    }

    auto send(std::span<std::byte const> data) const
        -> cppx::async::task<std::expected<std::size_t, cppx::http::net_error>> {
        while (true) {
            auto n = ::send(fd_, data.data(), data.size(), 0);
            if (n >= 0) co_return static_cast<std::size_t>(n);
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                co_await wait_writable(fd_);
                continue;
            }
            co_return std::unexpected(cppx::http::net_error::send_failed);
        }
    }

    auto recv(std::span<std::byte> buf) const
        -> cppx::async::task<std::expected<std::size_t, cppx::http::net_error>> {
        while (true) {
            auto n = ::recv(fd_, buf.data(), buf.size(), 0);
            if (n > 0) co_return static_cast<std::size_t>(n);
            if (n == 0)
                co_return std::unexpected(
                    cppx::http::net_error::connection_closed);
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                co_await wait_readable(fd_);
                continue;
            }
            co_return std::unexpected(cppx::http::net_error::recv_failed);
        }
    }

    void close() const {
        if (fd_ >= 0) ::close(fd_);
    }

    auto fd() const -> int { return fd_; }

private:
    friend class async_listener;
    explicit async_stream(int fd) : fd_{fd} {}
    int fd_;

    struct io_awaiter {
        int fd;
        bool writable; // false = readable

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h) const {
            if (current_loop) {
                if (writable)
                    current_loop->watch_writable(fd, h);
                else
                    current_loop->watch_readable(fd, h);
            }
        }

        void await_resume() const noexcept {}
    };

    static auto wait_readable(int fd) -> io_awaiter {
        return {fd, false};
    }
    static auto wait_writable(int fd) -> io_awaiter {
        return {fd, true};
    }
};

static_assert(cppx::http::async_stream_engine<async_stream>);

// ---- async POSIX listener ------------------------------------------------

class async_listener {
public:
    static auto bind(std::string_view host, std::uint16_t port)
        -> cppx::async::task<std::expected<async_listener, cppx::http::net_error>> {
        struct addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;
        auto port_str = std::to_string(port);
        auto host_str = std::string{host};
        struct addrinfo* res = nullptr;
        if (::getaddrinfo(host_str.empty() ? nullptr : host_str.c_str(),
                          port_str.c_str(), &hints, &res) != 0 || !res)
            co_return std::unexpected(cppx::http::net_error::resolve_failed);

        int fd = ::socket(res->ai_family, res->ai_socktype,
                          res->ai_protocol);
        if (fd < 0) {
            ::freeaddrinfo(res);
            co_return std::unexpected(cppx::http::net_error::bind_failed);
        }

        int opt = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        if (::bind(fd, res->ai_addr, res->ai_addrlen) < 0) {
            ::freeaddrinfo(res);
            ::close(fd);
            co_return std::unexpected(cppx::http::net_error::bind_failed);
        }
        ::freeaddrinfo(res);

        if (::listen(fd, 128) < 0) {
            ::close(fd);
            co_return std::unexpected(cppx::http::net_error::bind_failed);
        }

        // Set non-blocking for async accept.
        int flags = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        co_return async_listener{fd};
    }

    auto accept() const
        -> cppx::async::task<std::expected<async_stream, cppx::http::net_error>> {
        while (true) {
            struct sockaddr_storage addr;
            socklen_t len = sizeof(addr);
            int client = ::accept(fd_,
                                  reinterpret_cast<struct sockaddr*>(&addr),
                                  &len);
            if (client >= 0) {
                int flags = ::fcntl(client, F_GETFL, 0);
                ::fcntl(client, F_SETFL, flags | O_NONBLOCK);
                co_return async_stream{client};
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                co_await wait_readable();
                continue;
            }
            co_return std::unexpected(cppx::http::net_error::accept_failed);
        }
    }

    void close() const {
        if (fd_ >= 0) ::close(fd_);
    }

    auto local_port() const -> std::uint16_t {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        ::getsockname(fd_, reinterpret_cast<struct sockaddr*>(&addr), &len);
        return ntohs(addr.sin_port);
    }

private:
    explicit async_listener(int fd) : fd_{fd} {}
    int fd_;

    struct accept_awaiter {
        int fd;
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) const {
            if (current_loop)
                current_loop->watch_readable(fd, h);
        }
        void await_resume() const noexcept {}
    };

    auto wait_readable() const -> accept_awaiter {
        return {fd_};
    }
};

static_assert(cppx::http::async_listener_engine<async_listener, async_stream>);

// ---- run — convenience to drive a task on an event loop ------------------

template <class T>
auto run(cppx::async::task<T>& t) -> T {
    event_loop loop;
    auto* prev = current_loop;
    current_loop = &loop;
    loop.schedule(t.handle());
    loop.run();
    current_loop = prev;
    return t.result();
}

inline void run(cppx::async::task<void>& t) {
    event_loop loop;
    auto* prev = current_loop;
    current_loop = &loop;
    loop.schedule(t.handle());
    loop.run();
    current_loop = prev;
    t.result();
}

} // namespace cppx::async::system

#else
// wasm32-wasi / Windows stub — no async system support yet.
export module cppx.async.system;
#endif
