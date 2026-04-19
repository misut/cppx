// Platform event loop and async I/O. This is the impure edge for
// cppx.async — it wraps kqueue (macOS), epoll (Linux), or a
// select-based WinSock loop behind the executor_engine concept.
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

export module cppx.async.system;

#if !defined(__wasi__)
import std;
import cppx.async;
import cppx.net;
import cppx.net.async;

export namespace cppx::async::system {

class event_loop;
inline thread_local event_loop* current_loop = nullptr;

namespace detail {

template <class F>
class scope_guard {
public:
    explicit scope_guard(F fn) : fn_{std::move(fn)} {}

    scope_guard(scope_guard const&) = delete;
    auto operator=(scope_guard const&) -> scope_guard& = delete;

    scope_guard(scope_guard&& other) noexcept
        : fn_{std::move(other.fn_)},
          active_{std::exchange(other.active_, false)} {}

    ~scope_guard() {
        if (active_)
            fn_();
    }

private:
    F fn_;
    bool active_ = true;
};

template <class F>
scope_guard(F) -> scope_guard<F>;

#if defined(_WIN32)
struct wsa_session {
    wsa_session() {
        WSADATA data{};
        if (::WSAStartup(MAKEWORD(2, 2), &data) != 0)
            throw std::runtime_error{"WSAStartup failed"};
    }

    ~wsa_session() {
        ::WSACleanup();
    }
};

inline void ensure_wsa() {
    static auto session = wsa_session{};
    (void)session;
}

using native_socket = SOCKET;
inline constexpr native_socket invalid_socket = INVALID_SOCKET;

inline void close_socket(native_socket fd) {
    if (fd != invalid_socket)
        ::closesocket(fd);
}

inline auto last_socket_error() -> int {
    return ::WSAGetLastError();
}

inline auto would_block(int error) -> bool {
    return error == WSAEWOULDBLOCK
        || error == WSAEINPROGRESS
        || error == WSAEALREADY;
}

inline void set_nonblocking(native_socket fd) {
    u_long enabled = 1;
    ::ioctlsocket(fd, FIONBIO, &enabled);
}

inline auto create_socket_pair()
    -> std::expected<std::pair<native_socket, native_socket>, cppx::net::net_error> {
    ensure_wsa();

    auto listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == invalid_socket)
        return std::unexpected(cppx::net::net_error::bind_failed);

    auto cleanup_listener = scope_guard([&] {
        close_socket(listener);
    });

    auto const loopback = std::uint32_t{htonl(INADDR_LOOPBACK)};
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = loopback;
    address.sin_port = 0;

    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
        return std::unexpected(cppx::net::net_error::bind_failed);
    if (::listen(listener, 1) != 0)
        return std::unexpected(cppx::net::net_error::bind_failed);

    auto length = static_cast<int>(sizeof(address));
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0)
        return std::unexpected(cppx::net::net_error::bind_failed);

    auto writer = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (writer == invalid_socket)
        return std::unexpected(cppx::net::net_error::connect_refused);

    if (::connect(writer, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        close_socket(writer);
        return std::unexpected(cppx::net::net_error::connect_refused);
    }

    auto reader = ::accept(listener, nullptr, nullptr);
    if (reader == invalid_socket) {
        close_socket(writer);
        return std::unexpected(cppx::net::net_error::accept_failed);
    }

    return std::pair{reader, writer};
}

#else
using native_socket = int;
inline constexpr native_socket invalid_socket = -1;

inline void ensure_wsa() {}

inline void close_socket(native_socket fd) {
    if (fd >= 0)
        ::close(fd);
}

inline auto last_socket_error() -> int {
    return errno;
}

inline auto would_block(int error) -> bool {
    return error == EAGAIN
        || error == EWOULDBLOCK
        || error == EINPROGRESS;
}

inline void set_nonblocking(native_socket fd) {
    auto const flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
#endif

struct resolved_addresses {
    addrinfo* head = nullptr;

    resolved_addresses() = default;
    explicit resolved_addresses(addrinfo* value) : head{value} {}

    resolved_addresses(resolved_addresses const&) = delete;
    auto operator=(resolved_addresses const&) -> resolved_addresses& = delete;

    resolved_addresses(resolved_addresses&& other) noexcept
        : head{std::exchange(other.head, nullptr)} {}

    auto operator=(resolved_addresses&& other) noexcept
        -> resolved_addresses& {
        if (this != &other) {
            if (head)
                ::freeaddrinfo(head);
            head = std::exchange(other.head, nullptr);
        }
        return *this;
    }

    ~resolved_addresses() {
        if (head)
            ::freeaddrinfo(head);
    }

    auto get() const -> addrinfo* {
        return head;
    }
};

inline auto resolve_stream_addresses(std::string_view host, std::uint16_t port)
    -> std::expected<resolved_addresses, cppx::net::net_error> {
    auto port_string = std::to_string(port);
    auto host_string = std::string{host};

    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* result = nullptr;
    if (::getaddrinfo(
            host_string.c_str(),
            port_string.c_str(),
            &hints,
            &result) != 0
        || !result) {
        return std::unexpected(cppx::net::net_error::resolve_failed);
    }

    return resolved_addresses{result};
}

inline auto resolve_listener_addresses(std::string_view host, std::uint16_t port)
    -> std::expected<resolved_addresses, cppx::net::net_error> {
    auto port_string = std::to_string(port);
    auto host_string = std::string{host};

    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo* result = nullptr;
    if (::getaddrinfo(
            host_string.empty() ? nullptr : host_string.c_str(),
            port_string.c_str(),
            &hints,
            &result) != 0
        || !result) {
        return std::unexpected(cppx::net::net_error::resolve_failed);
    }

    return resolved_addresses{result};
}

inline auto open_socket(addrinfo const& endpoint,
                        cppx::net::net_error error)
    -> std::expected<native_socket, cppx::net::net_error> {
    auto fd = ::socket(
        endpoint.ai_family,
        endpoint.ai_socktype,
        endpoint.ai_protocol);
    if (fd == invalid_socket)
        return std::unexpected(error);
    return fd;
}

inline auto connect_socket(native_socket fd, addrinfo const& endpoint) -> int {
#if defined(_WIN32)
    return ::connect(
        fd,
        endpoint.ai_addr,
        static_cast<int>(endpoint.ai_addrlen));
#else
    return ::connect(fd, endpoint.ai_addr, endpoint.ai_addrlen);
#endif
}

inline auto complete_nonblocking_connect(native_socket fd)
    -> std::expected<void, cppx::net::net_error> {
    int socket_error = 0;
#if defined(_WIN32)
    auto length = static_cast<int>(sizeof(socket_error));
    ::getsockopt(
        fd,
        SOL_SOCKET,
        SO_ERROR,
        reinterpret_cast<char*>(&socket_error),
        &length);
#else
    socklen_t length = sizeof(socket_error);
    ::getsockopt(
        fd,
        SOL_SOCKET,
        SO_ERROR,
        &socket_error,
        &length);
#endif
    if (socket_error != 0)
        return std::unexpected(cppx::net::net_error::connect_refused);
    return {};
}

inline auto set_reuseaddr(native_socket fd) -> void {
    auto const reuse = 1;
#if defined(_WIN32)
    ::setsockopt(
        fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<char const*>(&reuse),
        sizeof(reuse));
#else
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
}

inline auto network_to_host_u16(std::uint16_t value) -> std::uint16_t {
    if constexpr (std::endian::native == std::endian::little)
        return std::byteswap(value);
    return value;
}

inline auto local_port(native_socket fd) -> std::uint16_t {
    sockaddr_storage address{};
#if defined(_WIN32)
    auto length = static_cast<int>(sizeof(address));
    if (::getsockname(
            fd,
            reinterpret_cast<sockaddr*>(&address),
            &length) != 0) {
        return 0;
    }
#else
    socklen_t length = sizeof(address);
    if (::getsockname(
            fd,
            reinterpret_cast<sockaddr*>(&address),
            &length) != 0) {
        return 0;
    }
#endif

    if (address.ss_family == AF_INET) {
        return network_to_host_u16(
            reinterpret_cast<sockaddr_in const*>(&address)->sin_port);
    }
    if (address.ss_family == AF_INET6) {
        return network_to_host_u16(
            reinterpret_cast<sockaddr_in6 const*>(&address)->sin6_port);
    }
    return 0;
}

} // namespace detail

using native_socket = detail::native_socket;

class event_loop {
public:
    event_loop() {
#if defined(__APPLE__)
        fd_ = ::kqueue();
        int fds[2];
        ::pipe(fds);
        wake_read_ = fds[0];
        wake_write_ = fds[1];
        detail::set_nonblocking(wake_read_);
        detail::set_nonblocking(wake_write_);

        struct kevent event;
        EV_SET(&event, wake_read_, EVFILT_READ, EV_ADD, 0, 0, nullptr);
        ::kevent(fd_, &event, 1, nullptr, 0, nullptr);
#elif defined(__linux__)
        fd_ = ::epoll_create1(0);
        int fds[2];
        ::pipe(fds);
        wake_read_ = fds[0];
        wake_write_ = fds[1];
        detail::set_nonblocking(wake_read_);
        detail::set_nonblocking(wake_write_);

        struct epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = wake_read_;
        ::epoll_ctl(fd_, EPOLL_CTL_ADD, wake_read_, &event);
#elif defined(_WIN32)
        detail::ensure_wsa();
        auto wake_pair = detail::create_socket_pair();
        if (!wake_pair)
            throw std::runtime_error{"failed to create async wake socket pair"};
        wake_read_ = wake_pair->first;
        wake_write_ = wake_pair->second;
        detail::set_nonblocking(wake_read_);
        detail::set_nonblocking(wake_write_);
#endif
    }

    ~event_loop() {
#if defined(__APPLE__) || defined(__linux__)
        if (fd_ >= 0)
            ::close(fd_);
        if (wake_read_ >= 0)
            ::close(wake_read_);
        if (wake_write_ >= 0)
            ::close(wake_write_);
#elif defined(_WIN32)
        detail::close_socket(wake_read_);
        detail::close_socket(wake_write_);
#endif
    }

    event_loop(event_loop const&) = delete;
    auto operator=(event_loop const&) -> event_loop& = delete;

    void schedule(std::coroutine_handle<> handle) {
        ready_.push_back(handle);
        wakeup();
    }

    void run() {
        auto* previous = current_loop;
        current_loop = this;
        auto restore = detail::scope_guard([&] {
            current_loop = previous;
        });

        running_ = true;
        while (running_) {
            while (!ready_.empty()) {
                auto handle = ready_.front();
                ready_.pop_front();
                if (!handle.done())
                    handle.resume();
            }

            if (!running_)
                break;

#if defined(_WIN32)
            if (ready_.empty()
                && read_waiters_.empty()
                && write_waiters_.empty()
                && timers_.empty()) {
                break;
            }
#else
            if (ready_.empty() && io_count_ == 0 && timers_.empty())
                break;
#endif

            wait_for_events();
        }
    }

    void stop() {
        running_ = false;
        wakeup();
    }

    void watch_readable(native_socket fd, std::coroutine_handle<> handle) {
#if defined(_WIN32)
        if (!read_waiters_.contains(fd))
            ++io_count_;
        read_waiters_[fd] = handle;
#elif defined(__APPLE__)
        ++io_count_;
        struct kevent event;
        EV_SET(&event, fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0,
               reinterpret_cast<void*>(handle.address()));
        ::kevent(fd_, &event, 1, nullptr, 0, nullptr);
#elif defined(__linux__)
        ++io_count_;
        struct epoll_event event{};
        event.events = EPOLLIN | EPOLLONESHOT;
        event.data.ptr = handle.address();
        if (::epoll_ctl(fd_, EPOLL_CTL_MOD, fd, &event) < 0)
            ::epoll_ctl(fd_, EPOLL_CTL_ADD, fd, &event);
#endif
    }

    void watch_writable(native_socket fd, std::coroutine_handle<> handle) {
#if defined(_WIN32)
        if (!write_waiters_.contains(fd))
            ++io_count_;
        write_waiters_[fd] = handle;
#elif defined(__APPLE__)
        ++io_count_;
        struct kevent event;
        EV_SET(&event, fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0,
               reinterpret_cast<void*>(handle.address()));
        ::kevent(fd_, &event, 1, nullptr, 0, nullptr);
#elif defined(__linux__)
        ++io_count_;
        struct epoll_event event{};
        event.events = EPOLLOUT | EPOLLONESHOT;
        event.data.ptr = handle.address();
        if (::epoll_ctl(fd_, EPOLL_CTL_MOD, fd, &event) < 0)
            ::epoll_ctl(fd_, EPOLL_CTL_ADD, fd, &event);
#endif
    }

    void schedule_after(
        std::chrono::steady_clock::duration duration,
        std::coroutine_handle<> handle) {
        timers_.push({std::chrono::steady_clock::now() + duration, handle});
        wakeup();
    }

private:
    struct timer_entry {
        std::chrono::steady_clock::time_point when;
        std::coroutine_handle<> handle;

        auto operator>(timer_entry const& other) const -> bool {
            return when > other.when;
        }
    };

    void wakeup() {
#if defined(__APPLE__) || defined(__linux__)
        auto const byte = char{1};
        [[maybe_unused]] auto const ignored =
            ::write(wake_write_, &byte, sizeof(byte));
#elif defined(_WIN32)
        auto const byte = char{1};
        [[maybe_unused]] auto const ignored =
            ::send(wake_write_, &byte, 1, 0);
#endif
    }

    void drain_wakeup() {
#if defined(__APPLE__) || defined(__linux__)
        auto buffer = std::array<char, 64>{};
        while (::read(wake_read_, buffer.data(), buffer.size()) > 0) {}
#elif defined(_WIN32)
        auto buffer = std::array<char, 64>{};
        while (::recv(wake_read_, buffer.data(), static_cast<int>(buffer.size()), 0) > 0) {}
#endif
    }

    auto compute_timeout_ms() const -> int {
        if (!ready_.empty())
            return 0;
        if (timers_.empty())
            return 500;

        auto const now = std::chrono::steady_clock::now();
        auto const delta = timers_.top().when - now;
        if (delta <= std::chrono::steady_clock::duration::zero())
            return 0;
        return static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(delta).count());
    }

    void fire_expired_timers() {
        auto const now = std::chrono::steady_clock::now();
        while (!timers_.empty() && timers_.top().when <= now) {
            ready_.push_back(timers_.top().handle);
            timers_.pop();
        }
    }

    void wait_for_events() {
        fire_expired_timers();
        if (!ready_.empty())
            return;

        auto const timeout_ms = compute_timeout_ms();

#if defined(__APPLE__)
        struct kevent events[64];
        struct timespec timeout{};
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_nsec = (timeout_ms % 1000) * 1000000L;
        auto const count = ::kevent(fd_, nullptr, 0, events, 64, &timeout);
        for (int i = 0; i < count; ++i) {
            if (static_cast<int>(events[i].ident) == wake_read_) {
                drain_wakeup();
            } else if (events[i].udata) {
                --io_count_;
                ready_.push_back(
                    std::coroutine_handle<>::from_address(events[i].udata));
            }
        }
#elif defined(__linux__)
        struct epoll_event events[64];
        auto const count = ::epoll_wait(fd_, events, 64, timeout_ms);
        for (int i = 0; i < count; ++i) {
            if (events[i].data.fd == wake_read_) {
                drain_wakeup();
            } else if (events[i].data.ptr) {
                --io_count_;
                ready_.push_back(
                    std::coroutine_handle<>::from_address(events[i].data.ptr));
            }
        }
#elif defined(_WIN32)
        fd_set readfds{};
        fd_set writefds{};
        fd_set exceptfds{};
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_ZERO(&exceptfds);
        FD_SET(wake_read_, &readfds);

        for (auto const& [fd, _] : read_waiters_)
            FD_SET(fd, &readfds);
        for (auto const& [fd, _] : write_waiters_) {
            FD_SET(fd, &writefds);
            FD_SET(fd, &exceptfds);
        }

        auto timeout = timeval{};
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
        [[maybe_unused]] auto const count =
            ::select(0, &readfds, &writefds, &exceptfds, &timeout);

        if (FD_ISSET(wake_read_, &readfds))
            drain_wakeup();

        auto ready_reads = std::vector<native_socket>{};
        for (auto const& [fd, _] : read_waiters_) {
            if (FD_ISSET(fd, &readfds))
                ready_reads.push_back(fd);
        }
        for (auto const fd : ready_reads) {
            auto const handle = read_waiters_[fd];
            read_waiters_.erase(fd);
            --io_count_;
            ready_.push_back(handle);
        }

        auto ready_writes = std::vector<native_socket>{};
        for (auto const& [fd, _] : write_waiters_) {
            if (FD_ISSET(fd, &writefds) || FD_ISSET(fd, &exceptfds))
                ready_writes.push_back(fd);
        }
        for (auto const fd : ready_writes) {
            auto const handle = write_waiters_[fd];
            write_waiters_.erase(fd);
            --io_count_;
            ready_.push_back(handle);
        }
#endif

        fire_expired_timers();
    }

#if defined(__APPLE__) || defined(__linux__)
    int fd_ = -1;
    int wake_read_ = -1;
    int wake_write_ = -1;
#elif defined(_WIN32)
    native_socket wake_read_ = detail::invalid_socket;
    native_socket wake_write_ = detail::invalid_socket;
    std::unordered_map<native_socket, std::coroutine_handle<>> read_waiters_;
    std::unordered_map<native_socket, std::coroutine_handle<>> write_waiters_;
#endif
    bool running_ = false;
    int io_count_ = 0;
    std::deque<std::coroutine_handle<>> ready_;
    std::priority_queue<
        timer_entry,
        std::vector<timer_entry>,
        std::greater<>> timers_;
};

static_assert(cppx::async::executor_engine<event_loop>);

class sleep_for_awaiter {
public:
    explicit sleep_for_awaiter(std::chrono::steady_clock::duration duration)
        : duration_{duration} {}

    auto await_ready() const noexcept -> bool {
        return duration_ <= std::chrono::steady_clock::duration::zero();
    }

    auto await_suspend(std::coroutine_handle<> handle) const -> bool {
        if (!current_loop)
            return false;
        current_loop->schedule_after(duration_, handle);
        return true;
    }

    void await_resume() const noexcept {}

private:
    std::chrono::steady_clock::duration duration_;
};

inline auto sleep_for(std::chrono::steady_clock::duration duration)
    -> sleep_for_awaiter {
    return sleep_for_awaiter{duration};
}

class async_stream {
public:
    async_stream() = default;
    async_stream(async_stream const&) = delete;
    auto operator=(async_stream const&) -> async_stream& = delete;

    async_stream(async_stream&& other) noexcept
        : fd_{std::exchange(other.fd_, detail::invalid_socket)} {}

    auto operator=(async_stream&& other) noexcept -> async_stream& {
        if (this != &other) {
            close();
            fd_ = std::exchange(other.fd_, detail::invalid_socket);
        }
        return *this;
    }

    ~async_stream() {
        close();
    }

    static auto connect(std::string_view host, std::uint16_t port)
        -> cppx::async::task<std::expected<async_stream, cppx::net::net_error>> {
        detail::ensure_wsa();

        auto endpoints = detail::resolve_stream_addresses(host, port);
        if (!endpoints)
            co_return std::unexpected(endpoints.error());

        auto fd = detail::open_socket(*endpoints->get(),
                                      cppx::net::net_error::connect_refused);
        if (!fd)
            co_return std::unexpected(fd.error());

        detail::set_nonblocking(*fd);

#if defined(_WIN32)
        auto const rc = detail::connect_socket(*fd, *endpoints->get());
        if (rc == SOCKET_ERROR) {
            auto const error = detail::last_socket_error();
            if (!detail::would_block(error)) {
                detail::close_socket(*fd);
                co_return std::unexpected(cppx::net::net_error::connect_refused);
            }

            co_await wait_writable(*fd);
            auto connected = detail::complete_nonblocking_connect(*fd);
            if (!connected) {
                detail::close_socket(*fd);
                co_return std::unexpected(cppx::net::net_error::connect_refused);
            }
        }
#else
        auto const rc = detail::connect_socket(*fd, *endpoints->get());
        if (rc < 0) {
            if (errno != EINPROGRESS) {
                detail::close_socket(*fd);
                co_return std::unexpected(cppx::net::net_error::connect_refused);
            }

            co_await wait_writable(*fd);
            auto connected = detail::complete_nonblocking_connect(*fd);
            if (!connected) {
                detail::close_socket(*fd);
                co_return std::unexpected(cppx::net::net_error::connect_refused);
            }
        }
#endif

        co_return async_stream{*fd};
    }

    auto send(std::span<std::byte const> data)
        -> cppx::async::task<std::expected<std::size_t, cppx::net::net_error>> {
        while (true) {
#if defined(_WIN32)
            auto const sent = ::send(
                fd_,
                reinterpret_cast<char const*>(data.data()),
                static_cast<int>(data.size()),
                0);
            if (sent != SOCKET_ERROR)
                co_return static_cast<std::size_t>(sent);

            if (detail::would_block(detail::last_socket_error())) {
                co_await wait_writable(fd_);
                continue;
            }
#else
            auto const sent = ::send(fd_, data.data(), data.size(), 0);
            if (sent >= 0)
                co_return static_cast<std::size_t>(sent);

            if (detail::would_block(errno)) {
                co_await wait_writable(fd_);
                continue;
            }
#endif
            co_return std::unexpected(cppx::net::net_error::send_failed);
        }
    }

    auto recv(std::span<std::byte> buffer)
        -> cppx::async::task<std::expected<std::size_t, cppx::net::net_error>> {
        while (true) {
#if defined(_WIN32)
            auto const received = ::recv(
                fd_,
                reinterpret_cast<char*>(buffer.data()),
                static_cast<int>(buffer.size()),
                0);
            if (received > 0)
                co_return static_cast<std::size_t>(received);
            if (received == 0)
                co_return std::unexpected(cppx::net::net_error::connection_closed);

            if (detail::would_block(detail::last_socket_error())) {
                co_await wait_readable(fd_);
                continue;
            }
#else
            auto const received = ::recv(fd_, buffer.data(), buffer.size(), 0);
            if (received > 0)
                co_return static_cast<std::size_t>(received);
            if (received == 0)
                co_return std::unexpected(cppx::net::net_error::connection_closed);

            if (detail::would_block(errno)) {
                co_await wait_readable(fd_);
                continue;
            }
#endif
            co_return std::unexpected(cppx::net::net_error::recv_failed);
        }
    }

    void close() {
        if (fd_ != detail::invalid_socket) {
            detail::close_socket(fd_);
            fd_ = detail::invalid_socket;
        }
    }

    auto native_handle() const -> native_socket {
        return fd_;
    }

private:
    friend class async_listener;

    explicit async_stream(native_socket fd) : fd_{fd} {}

    struct io_awaiter {
        native_socket fd;
        bool writable = false;

        auto await_ready() const noexcept -> bool {
            return false;
        }

        void await_suspend(std::coroutine_handle<> handle) const {
            if (!current_loop)
                return;
            if (writable)
                current_loop->watch_writable(fd, handle);
            else
                current_loop->watch_readable(fd, handle);
        }

        void await_resume() const noexcept {}
    };

    static auto wait_readable(native_socket fd) -> io_awaiter {
        return io_awaiter{fd, false};
    }

    static auto wait_writable(native_socket fd) -> io_awaiter {
        return io_awaiter{fd, true};
    }

    native_socket fd_ = detail::invalid_socket;
};

static_assert(cppx::net::async::stream_engine<async_stream>);

class async_listener {
public:
    async_listener() = default;
    async_listener(async_listener const&) = delete;
    auto operator=(async_listener const&) -> async_listener& = delete;

    async_listener(async_listener&& other) noexcept
        : fd_{std::exchange(other.fd_, detail::invalid_socket)} {}

    auto operator=(async_listener&& other) noexcept -> async_listener& {
        if (this != &other) {
            close();
            fd_ = std::exchange(other.fd_, detail::invalid_socket);
        }
        return *this;
    }

    ~async_listener() {
        close();
    }

    static auto bind(std::string_view host, std::uint16_t port)
        -> cppx::async::task<std::expected<async_listener, cppx::net::net_error>> {
        detail::ensure_wsa();

        auto endpoints = detail::resolve_listener_addresses(host, port);
        if (!endpoints)
            co_return std::unexpected(endpoints.error());

        auto fd = detail::open_socket(*endpoints->get(),
                                      cppx::net::net_error::bind_failed);
        if (!fd)
            co_return std::unexpected(fd.error());

        detail::set_reuseaddr(*fd);

#if defined(_WIN32)
        if (::bind(
                *fd,
                endpoints->get()->ai_addr,
                static_cast<int>(endpoints->get()->ai_addrlen)) != 0) {
#else
        if (::bind(*fd, endpoints->get()->ai_addr, endpoints->get()->ai_addrlen) != 0) {
#endif
            detail::close_socket(*fd);
            co_return std::unexpected(cppx::net::net_error::bind_failed);
        }

        if (::listen(*fd, 128) != 0) {
            detail::close_socket(*fd);
            co_return std::unexpected(cppx::net::net_error::bind_failed);
        }

        detail::set_nonblocking(*fd);
        co_return async_listener{*fd};
    }

    auto accept()
        -> cppx::async::task<std::expected<async_stream, cppx::net::net_error>> {
        while (true) {
#if defined(_WIN32)
            auto client = ::accept(fd_, nullptr, nullptr);
            if (client != detail::invalid_socket) {
                detail::set_nonblocking(client);
                co_return async_stream{client};
            }

            if (detail::would_block(detail::last_socket_error())) {
                co_await wait_readable();
                continue;
            }
#else
            sockaddr_storage address{};
            socklen_t length = sizeof(address);
            auto client = ::accept(
                fd_,
                reinterpret_cast<sockaddr*>(&address),
                &length);
            if (client >= 0) {
                detail::set_nonblocking(client);
                co_return async_stream{client};
            }

            if (detail::would_block(errno)) {
                co_await wait_readable();
                continue;
            }
#endif
            co_return std::unexpected(cppx::net::net_error::accept_failed);
        }
    }

    void close() {
        if (fd_ != detail::invalid_socket) {
            detail::close_socket(fd_);
            fd_ = detail::invalid_socket;
        }
    }

    auto local_port() const -> std::uint16_t {
        return detail::local_port(fd_);
    }

private:
    explicit async_listener(native_socket fd) : fd_{fd} {}

    struct accept_awaiter {
        native_socket fd;

        auto await_ready() const noexcept -> bool {
            return false;
        }

        void await_suspend(std::coroutine_handle<> handle) const {
            if (current_loop)
                current_loop->watch_readable(fd, handle);
        }

        void await_resume() const noexcept {}
    };

    auto wait_readable() const -> accept_awaiter {
        return accept_awaiter{fd_};
    }

    native_socket fd_ = detail::invalid_socket;
};

static_assert(cppx::net::async::listener_engine<async_listener, async_stream>);

template <class T>
auto run(cppx::async::task<T>& task) -> T {
    auto loop = event_loop{};
    loop.schedule(task.handle());
    loop.run();
    return task.result();
}

inline void run(cppx::async::task<void>& task) {
    auto loop = event_loop{};
    loop.schedule(task.handle());
    loop.run();
    task.result();
}

} // namespace cppx::async::system

#endif // !defined(__wasi__)
