// Platform socket engines for cppx.http. This is the only impure module
// in the HTTP stack — it wraps POSIX sockets (macOS/Linux) and WinSock
// (Windows) behind the stream_engine / listener_engine concepts defined
// in cppx.http.
//
// TLS providers will be added in a follow-up phase.

module;

// Global module fragment: platform socket headers. C++23 modules
// cannot import these — they must be #included here.
#if defined(__APPLE__) || defined(__linux__)
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cerrno>
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

export module cppx.http.system;
import std;
import cppx.http;

export namespace cppx::http::system {

// ---- POSIX socket engine (macOS + Linux) ---------------------------------

#if defined(__APPLE__) || defined(__linux__)

class posix_listener; // forward for friend

class posix_stream {
    int fd_ = -1;

    explicit posix_stream(int fd) : fd_{fd} {}
    friend class posix_listener;

public:
    posix_stream() = default;
    posix_stream(posix_stream const&) = delete;
    posix_stream& operator=(posix_stream const&) = delete;
    posix_stream(posix_stream&& o) noexcept : fd_{std::exchange(o.fd_, -1)} {}
    posix_stream& operator=(posix_stream&& o) noexcept {
        if (this != &o) { close(); fd_ = std::exchange(o.fd_, -1); }
        return *this;
    }
    ~posix_stream() { close(); }

    static auto connect(std::string_view host, std::uint16_t port)
        -> std::expected<posix_stream, cppx::http::net_error>
    {
        auto host_str = std::string{host};
        auto port_str = std::to_string(port);

        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        struct addrinfo* result = nullptr;
        if (::getaddrinfo(host_str.c_str(), port_str.c_str(),
                          &hints, &result) != 0)
            return std::unexpected(cppx::http::net_error::resolve_failed);

        int fd = -1;
        for (auto* rp = result; rp; rp = rp->ai_next) {
            fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (fd < 0) continue;
            if (::connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
            ::close(fd);
            fd = -1;
        }
        ::freeaddrinfo(result);

        if (fd < 0)
            return std::unexpected(cppx::http::net_error::connect_refused);

        // Disable Nagle's algorithm for low-latency HTTP
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        return posix_stream{fd};
    }

    auto send(std::span<std::byte const> data) const
        -> std::expected<std::size_t, cppx::http::net_error>
    {
        auto n = ::send(fd_, data.data(), data.size(), 0);
        if (n < 0)
            return std::unexpected(cppx::http::net_error::send_failed);
        return static_cast<std::size_t>(n);
    }

    auto recv(std::span<std::byte> buf) const
        -> std::expected<std::size_t, cppx::http::net_error>
    {
        auto n = ::recv(fd_, buf.data(), buf.size(), 0);
        if (n < 0)
            return std::unexpected(cppx::http::net_error::recv_failed);
        if (n == 0)
            return std::unexpected(cppx::http::net_error::connection_closed);
        return static_cast<std::size_t>(n);
    }

    void close() {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }

    // Extra (not part of concept): raw fd for platform-specific extensions.
    int native_handle() const { return fd_; }
};

class posix_listener {
    int fd_ = -1;

    explicit posix_listener(int fd) : fd_{fd} {}

public:
    posix_listener() = default;
    posix_listener(posix_listener const&) = delete;
    posix_listener& operator=(posix_listener const&) = delete;
    posix_listener(posix_listener&& o) noexcept
        : fd_{std::exchange(o.fd_, -1)} {}
    posix_listener& operator=(posix_listener&& o) noexcept {
        if (this != &o) { close(); fd_ = std::exchange(o.fd_, -1); }
        return *this;
    }
    ~posix_listener() { close(); }

    static auto bind(std::string_view host, std::uint16_t port)
        -> std::expected<posix_listener, cppx::http::net_error>
    {
        auto host_str = std::string{host};
        auto port_str = std::to_string(port);

        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;

        struct addrinfo* result = nullptr;
        if (::getaddrinfo(host_str.empty() ? nullptr : host_str.c_str(),
                          port_str.c_str(), &hints, &result) != 0)
            return std::unexpected(cppx::http::net_error::resolve_failed);

        int fd = -1;
        for (auto* rp = result; rp; rp = rp->ai_next) {
            fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (fd < 0) continue;
            int one = 1;
            ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
            if (::bind(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
            ::close(fd);
            fd = -1;
        }
        ::freeaddrinfo(result);

        if (fd < 0)
            return std::unexpected(cppx::http::net_error::bind_failed);

        if (::listen(fd, SOMAXCONN) < 0) {
            ::close(fd);
            return std::unexpected(cppx::http::net_error::bind_failed);
        }

        return posix_listener{fd};
    }

    auto accept() const
        -> std::expected<posix_stream, cppx::http::net_error>
    {
        struct sockaddr_storage addr{};
        socklen_t len = sizeof(addr);
        auto client = ::accept(fd_,
            reinterpret_cast<struct sockaddr*>(&addr), &len);
        if (client < 0)
            return std::unexpected(cppx::http::net_error::accept_failed);
        return posix_stream{client};
    }

    void close() {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }

    // Extra: get the actual bound port (useful with port 0 = ephemeral).
    auto local_port() const -> std::uint16_t {
        struct sockaddr_storage addr{};
        socklen_t len = sizeof(addr);
        if (::getsockname(fd_, reinterpret_cast<struct sockaddr*>(&addr),
                          &len) < 0)
            return 0;
        if (addr.ss_family == AF_INET)
            return ntohs(reinterpret_cast<struct sockaddr_in*>(&addr)->sin_port);
        if (addr.ss_family == AF_INET6)
            return ntohs(reinterpret_cast<struct sockaddr_in6*>(&addr)->sin6_port);
        return 0;
    }

    int native_handle() const { return fd_; }
};

using stream   = posix_stream;
using listener = posix_listener;

static_assert(cppx::http::stream_engine<stream>);
static_assert(cppx::http::listener_engine<listener, stream>);

#elif defined(_WIN32)

// ---- WinSock engine (Windows) --------------------------------------------

namespace detail {

// RAII WinSock initializer. Constructed once (static local).
struct wsa_init {
    wsa_init() {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
    }
    ~wsa_init() { WSACleanup(); }
};

inline void ensure_wsa() {
    static wsa_init init;
}

} // namespace detail

class win_listener; // forward for friend

class win_stream {
    SOCKET fd_ = INVALID_SOCKET;

    explicit win_stream(SOCKET fd) : fd_{fd} {}
    friend class win_listener;

public:
    win_stream() = default;
    win_stream(win_stream const&) = delete;
    win_stream& operator=(win_stream const&) = delete;
    win_stream(win_stream&& o) noexcept
        : fd_{std::exchange(o.fd_, INVALID_SOCKET)} {}
    win_stream& operator=(win_stream&& o) noexcept {
        if (this != &o) { close(); fd_ = std::exchange(o.fd_, INVALID_SOCKET); }
        return *this;
    }
    ~win_stream() { close(); }

    static auto connect(std::string_view host, std::uint16_t port)
        -> std::expected<win_stream, cppx::http::net_error>
    {
        detail::ensure_wsa();

        auto host_str = std::string{host};
        auto port_str = std::to_string(port);

        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        struct addrinfo* result = nullptr;
        if (::getaddrinfo(host_str.c_str(), port_str.c_str(),
                          &hints, &result) != 0)
            return std::unexpected(cppx::http::net_error::resolve_failed);

        SOCKET fd = INVALID_SOCKET;
        for (auto* rp = result; rp; rp = rp->ai_next) {
            fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (fd == INVALID_SOCKET) continue;
            if (::connect(fd, rp->ai_addr,
                    static_cast<int>(rp->ai_addrlen)) == 0)
                break;
            ::closesocket(fd);
            fd = INVALID_SOCKET;
        }
        ::freeaddrinfo(result);

        if (fd == INVALID_SOCKET)
            return std::unexpected(cppx::http::net_error::connect_refused);

        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                     reinterpret_cast<char const*>(&one), sizeof(one));

        return win_stream{fd};
    }

    auto send(std::span<std::byte const> data) const
        -> std::expected<std::size_t, cppx::http::net_error>
    {
        auto n = ::send(fd_, reinterpret_cast<char const*>(data.data()),
                        static_cast<int>(data.size()), 0);
        if (n == SOCKET_ERROR)
            return std::unexpected(cppx::http::net_error::send_failed);
        return static_cast<std::size_t>(n);
    }

    auto recv(std::span<std::byte> buf) const
        -> std::expected<std::size_t, cppx::http::net_error>
    {
        auto n = ::recv(fd_, reinterpret_cast<char*>(buf.data()),
                        static_cast<int>(buf.size()), 0);
        if (n == SOCKET_ERROR)
            return std::unexpected(cppx::http::net_error::recv_failed);
        if (n == 0)
            return std::unexpected(cppx::http::net_error::connection_closed);
        return static_cast<std::size_t>(n);
    }

    void close() {
        if (fd_ != INVALID_SOCKET) { ::closesocket(fd_); fd_ = INVALID_SOCKET; }
    }

    SOCKET native_handle() const { return fd_; }
};

class win_listener {
    SOCKET fd_ = INVALID_SOCKET;

    explicit win_listener(SOCKET fd) : fd_{fd} {}

public:
    win_listener() = default;
    win_listener(win_listener const&) = delete;
    win_listener& operator=(win_listener const&) = delete;
    win_listener(win_listener&& o) noexcept
        : fd_{std::exchange(o.fd_, INVALID_SOCKET)} {}
    win_listener& operator=(win_listener&& o) noexcept {
        if (this != &o) { close(); fd_ = std::exchange(o.fd_, INVALID_SOCKET); }
        return *this;
    }
    ~win_listener() { close(); }

    static auto bind(std::string_view host, std::uint16_t port)
        -> std::expected<win_listener, cppx::http::net_error>
    {
        detail::ensure_wsa();

        auto host_str = std::string{host};
        auto port_str = std::to_string(port);

        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;

        struct addrinfo* result = nullptr;
        if (::getaddrinfo(host_str.empty() ? nullptr : host_str.c_str(),
                          port_str.c_str(), &hints, &result) != 0)
            return std::unexpected(cppx::http::net_error::resolve_failed);

        SOCKET fd = INVALID_SOCKET;
        for (auto* rp = result; rp; rp = rp->ai_next) {
            fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (fd == INVALID_SOCKET) continue;
            int one = 1;
            ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                         reinterpret_cast<char const*>(&one), sizeof(one));
            if (::bind(fd, rp->ai_addr,
                    static_cast<int>(rp->ai_addrlen)) == 0)
                break;
            ::closesocket(fd);
            fd = INVALID_SOCKET;
        }
        ::freeaddrinfo(result);

        if (fd == INVALID_SOCKET)
            return std::unexpected(cppx::http::net_error::bind_failed);

        if (::listen(fd, SOMAXCONN) == SOCKET_ERROR) {
            ::closesocket(fd);
            return std::unexpected(cppx::http::net_error::bind_failed);
        }

        return win_listener{fd};
    }

    auto accept() const
        -> std::expected<win_stream, cppx::http::net_error>
    {
        struct sockaddr_storage addr{};
        int len = sizeof(addr);
        auto client = ::accept(fd_,
            reinterpret_cast<struct sockaddr*>(&addr), &len);
        if (client == INVALID_SOCKET)
            return std::unexpected(cppx::http::net_error::accept_failed);
        return win_stream{client};
    }

    void close() {
        if (fd_ != INVALID_SOCKET) { ::closesocket(fd_); fd_ = INVALID_SOCKET; }
    }

    auto local_port() const -> std::uint16_t {
        struct sockaddr_storage addr{};
        int len = sizeof(addr);
        if (::getsockname(fd_, reinterpret_cast<struct sockaddr*>(&addr),
                          &len) != 0)
            return 0;
        if (addr.ss_family == AF_INET)
            return ntohs(reinterpret_cast<struct sockaddr_in*>(&addr)->sin_port);
        if (addr.ss_family == AF_INET6)
            return ntohs(reinterpret_cast<struct sockaddr_in6*>(&addr)->sin6_port);
        return 0;
    }

    SOCKET native_handle() const { return fd_; }
};

using stream   = win_stream;
using listener = win_listener;

static_assert(cppx::http::stream_engine<stream>);
static_assert(cppx::http::listener_engine<listener, stream>);

#endif // platform

// ---- send_all / recv_all helpers -----------------------------------------

// Send all bytes, retrying partial sends.
template <cppx::http::stream_engine S>
auto send_all(S& s, std::span<std::byte const> data)
    -> std::expected<void, cppx::http::net_error>
{
    while (!data.empty()) {
        auto n = s.send(data);
        if (!n) return std::unexpected(n.error());
        data = data.subspan(*n);
    }
    return {};
}

// Receive until the buffer is full or connection closes.
template <cppx::http::stream_engine S>
auto recv_all(S& s, std::vector<std::byte>& out, std::size_t max_size = 64 * 1024 * 1024)
    -> std::expected<void, cppx::http::net_error>
{
    auto buf = std::array<std::byte, 8192>{};
    while (out.size() < max_size) {
        auto n = s.recv(buf);
        if (!n) {
            if (n.error() == cppx::http::net_error::connection_closed)
                return {};
            return std::unexpected(n.error());
        }
        out.insert(out.end(), buf.begin(), buf.begin() + *n);
    }
    return {};
}

} // namespace cppx::http::system
