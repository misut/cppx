// Platform socket engines and TLS providers for cppx.http. This is the
// only impure module in the HTTP stack — it wraps platform sockets and
// TLS behind the concepts defined in cppx.http.
//
// TLS backends:
//   macOS  — Security.framework (SecureTransport)
//   Linux  — system OpenSSL (libssl/libcrypto)
//   Windows — SChannel (SSPI)

module;

// Global module fragment: platform headers that C++23 modules can't import.
#if defined(__APPLE__) || defined(__linux__)
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cerrno>
#endif

#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <Security/Security.h>
#endif

#if defined(__linux__)
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#if defined(_WIN32)
#define SECURITY_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <security.h>
#include <schannel.h>
#include <sspi.h>
#pragma comment(lib, "secur32.lib")
#endif

export module cppx.http.system;
import std;
import cppx.http;
import cppx.http.client;

export namespace cppx::http::system {

// =========================================================================
// POSIX socket engine (macOS + Linux)
// =========================================================================

#if defined(__APPLE__) || defined(__linux__)

class posix_listener;

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
        if (::getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &result) != 0)
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
        if (fd < 0) return std::unexpected(cppx::http::net_error::connect_refused);
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        return posix_stream{fd};
    }

    auto send(std::span<std::byte const> data) const
        -> std::expected<std::size_t, cppx::http::net_error> {
        auto n = ::send(fd_, data.data(), data.size(), 0);
        if (n < 0) return std::unexpected(cppx::http::net_error::send_failed);
        return static_cast<std::size_t>(n);
    }

    auto recv(std::span<std::byte> buf) const
        -> std::expected<std::size_t, cppx::http::net_error> {
        auto n = ::recv(fd_, buf.data(), buf.size(), 0);
        if (n < 0) return std::unexpected(cppx::http::net_error::recv_failed);
        if (n == 0) return std::unexpected(cppx::http::net_error::connection_closed);
        return static_cast<std::size_t>(n);
    }

    void close() { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }
    int native_handle() const { return fd_; }
};

class posix_listener {
    int fd_ = -1;
    explicit posix_listener(int fd) : fd_{fd} {}
public:
    posix_listener() = default;
    posix_listener(posix_listener const&) = delete;
    posix_listener& operator=(posix_listener const&) = delete;
    posix_listener(posix_listener&& o) noexcept : fd_{std::exchange(o.fd_, -1)} {}
    posix_listener& operator=(posix_listener&& o) noexcept {
        if (this != &o) { close(); fd_ = std::exchange(o.fd_, -1); }
        return *this;
    }
    ~posix_listener() { close(); }

    static auto bind(std::string_view host, std::uint16_t port)
        -> std::expected<posix_listener, cppx::http::net_error> {
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
        if (fd < 0) return std::unexpected(cppx::http::net_error::bind_failed);
        if (::listen(fd, SOMAXCONN) < 0) {
            ::close(fd); return std::unexpected(cppx::http::net_error::bind_failed);
        }
        return posix_listener{fd};
    }

    auto accept() const -> std::expected<posix_stream, cppx::http::net_error> {
        struct sockaddr_storage addr{};
        socklen_t len = sizeof(addr);
        auto client = ::accept(fd_, reinterpret_cast<struct sockaddr*>(&addr), &len);
        if (client < 0) return std::unexpected(cppx::http::net_error::accept_failed);
        return posix_stream{client};
    }

    void close() { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }

    auto local_port() const -> std::uint16_t {
        struct sockaddr_storage addr{};
        socklen_t len = sizeof(addr);
        if (::getsockname(fd_, reinterpret_cast<struct sockaddr*>(&addr), &len) < 0)
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

// =========================================================================
// macOS TLS — SecureTransport
// =========================================================================

#if defined(__APPLE__)

namespace detail {

// SecureTransport I/O callbacks. The connection ref is a posix_stream*.
inline OSStatus apple_read_cb(SSLConnectionRef conn, void* data, size_t* len) {
    auto* s = static_cast<posix_stream*>(const_cast<void*>(conn));
    auto requested = *len;
    auto dst = static_cast<std::byte*>(data);
    std::size_t total = 0;
    while (total < requested) {
        auto buf = std::span<std::byte>{dst + total, requested - total};
        auto r = s->recv(buf);
        if (!r) {
            *len = total;
            if (r.error() == cppx::http::net_error::connection_closed)
                return total > 0 ? errSSLWouldBlock : errSSLClosedGraceful;
            return errSSLClosedAbort;
        }
        total += *r;
    }
    *len = total;
    return noErr;
}

inline OSStatus apple_write_cb(SSLConnectionRef conn, void const* data, size_t* len) {
    auto* s = static_cast<posix_stream*>(const_cast<void*>(conn));
    auto requested = *len;
    auto src = static_cast<std::byte const*>(data);
    std::size_t total = 0;
    while (total < requested) {
        auto buf = std::span<std::byte const>{src + total, requested - total};
        auto r = s->send(buf);
        if (!r) { *len = total; return errSSLClosedAbort; }
        total += *r;
    }
    *len = total;
    return noErr;
}

} // namespace detail

struct apple_tls; // forward for friend

class apple_tls_stream {
    // posix_stream is heap-allocated so its address stays stable
    // across moves — SecureTransport's SSLSetConnection holds a raw
    // pointer to it for the I/O callbacks.
    std::unique_ptr<posix_stream> raw_;
    SSLContextRef ctx_ = nullptr;

    apple_tls_stream(std::unique_ptr<posix_stream> raw, SSLContextRef ctx)
        : raw_{std::move(raw)}, ctx_{ctx} {}
    friend struct apple_tls;

    auto setup_and_handshake(std::string_view hostname)
        -> std::expected<void, cppx::http::net_error>
    {
        SSLSetIOFuncs(ctx_, detail::apple_read_cb, detail::apple_write_cb);
        SSLSetConnection(ctx_, raw_.get());
        auto host_str = std::string{hostname};
        SSLSetPeerDomainName(ctx_, host_str.c_str(), host_str.size());
        // Enable TLS 1.2 (SecureTransport max)
        SSLSetProtocolVersionMin(ctx_, kTLSProtocol12);
        SSLSetProtocolVersionMax(ctx_, kTLSProtocol12);
        OSStatus status;
        do { status = SSLHandshake(ctx_); } while (status == errSSLWouldBlock);
        if (status != noErr) {
            // Release ctx here so the destructor doesn't call SSLClose
            // on a partially-handshaked context (causes null deref in
            // SSLSendAlert).
            CFRelease(ctx_); ctx_ = nullptr;
            return std::unexpected(cppx::http::net_error::tls_handshake_failed);
        }
        return {};
    }

public:
    apple_tls_stream() = default;
    apple_tls_stream(apple_tls_stream const&) = delete;
    apple_tls_stream& operator=(apple_tls_stream const&) = delete;
    apple_tls_stream(apple_tls_stream&& o) noexcept
        : raw_{std::move(o.raw_)}, ctx_{std::exchange(o.ctx_, nullptr)} {}
    apple_tls_stream& operator=(apple_tls_stream&& o) noexcept {
        if (this != &o) {
            close();
            raw_ = std::move(o.raw_);
            ctx_ = std::exchange(o.ctx_, nullptr);
        }
        return *this;
    }
    ~apple_tls_stream() { close(); }

    static auto connect(std::string_view host, std::uint16_t port)
        -> std::expected<apple_tls_stream, cppx::http::net_error>
    {
        auto raw = posix_stream::connect(host, port);
        if (!raw) return std::unexpected(raw.error());
        auto ctx = SSLCreateContext(nullptr, kSSLClientSide, kSSLStreamType);
        if (!ctx) return std::unexpected(cppx::http::net_error::tls_handshake_failed);
        auto result = apple_tls_stream{
            std::make_unique<posix_stream>(std::move(*raw)), ctx};
        auto hr = result.setup_and_handshake(host);
        if (!hr) return std::unexpected(hr.error());
        return result;
    }

    auto send(std::span<std::byte const> data) const
        -> std::expected<std::size_t, cppx::http::net_error> {
        std::size_t written = 0;
        auto status = SSLWrite(ctx_, data.data(), data.size(), &written);
        if (status != noErr && written == 0)
            return std::unexpected(cppx::http::net_error::tls_write_failed);
        return written;
    }

    auto recv(std::span<std::byte> buf) const
        -> std::expected<std::size_t, cppx::http::net_error> {
        std::size_t read = 0;
        auto status = SSLRead(ctx_, buf.data(), buf.size(), &read);
        if (status == errSSLClosedGraceful || (status == errSSLClosedNoNotify && read == 0))
            return std::unexpected(cppx::http::net_error::connection_closed);
        if (status != noErr && read == 0)
            return std::unexpected(cppx::http::net_error::tls_read_failed);
        return read;
    }

    void close() {
        // Skip SSLClose — it sends a TLS close_notify alert which can
        // crash in SSLSendAlert when the underlying socket is in an
        // unexpected state (SecureTransport bug). Just release the
        // context and close the raw socket.
        if (ctx_) { CFRelease(ctx_); ctx_ = nullptr; }
        if (raw_) raw_->close();
    }
};

static_assert(cppx::http::stream_engine<apple_tls_stream>);

struct apple_tls {
    using stream = apple_tls_stream;
    auto wrap(posix_stream raw, std::string_view hostname) const
        -> std::expected<apple_tls_stream, cppx::http::net_error>
    {
        auto ctx = SSLCreateContext(nullptr, kSSLClientSide, kSSLStreamType);
        if (!ctx) return std::unexpected(cppx::http::net_error::tls_handshake_failed);
        auto result = apple_tls_stream{
            std::make_unique<posix_stream>(std::move(raw)), ctx};
        auto hr = result.setup_and_handshake(hostname);
        if (!hr) return std::unexpected(hr.error());
        return result;
    }
};

static_assert(cppx::http::tls_provider<apple_tls, posix_stream>);
using tls = apple_tls;

// =========================================================================
// Linux TLS — OpenSSL
// =========================================================================

#elif defined(__linux__)

namespace detail {
struct openssl_init {
    openssl_init() {
        SSL_library_init();
        SSL_load_error_strings();
    }
};
inline void ensure_openssl() { static openssl_init init; }
} // namespace detail

class openssl_tls_stream {
    posix_stream raw_;
    SSL_CTX* ctx_ = nullptr;
    SSL* ssl_ = nullptr;

    openssl_tls_stream(posix_stream raw, SSL_CTX* ctx, SSL* ssl)
        : raw_{std::move(raw)}, ctx_{ctx}, ssl_{ssl} {}

    friend struct openssl_tls;

public:
    openssl_tls_stream() = default;
    openssl_tls_stream(openssl_tls_stream const&) = delete;
    openssl_tls_stream& operator=(openssl_tls_stream const&) = delete;
    openssl_tls_stream(openssl_tls_stream&& o) noexcept
        : raw_{std::move(o.raw_)},
          ctx_{std::exchange(o.ctx_, nullptr)},
          ssl_{std::exchange(o.ssl_, nullptr)} {}
    openssl_tls_stream& operator=(openssl_tls_stream&& o) noexcept {
        if (this != &o) {
            close();
            raw_ = std::move(o.raw_);
            ctx_ = std::exchange(o.ctx_, nullptr);
            ssl_ = std::exchange(o.ssl_, nullptr);
        }
        return *this;
    }
    ~openssl_tls_stream() { close(); }

    static auto connect(std::string_view host, std::uint16_t port)
        -> std::expected<openssl_tls_stream, cppx::http::net_error>
    {
        detail::ensure_openssl();
        auto raw = posix_stream::connect(host, port);
        if (!raw) return std::unexpected(raw.error());
        auto* ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) return std::unexpected(cppx::http::net_error::tls_handshake_failed);
        SSL_CTX_set_default_verify_paths(ctx);
        auto* ssl = SSL_new(ctx);
        if (!ssl) { SSL_CTX_free(ctx); return std::unexpected(cppx::http::net_error::tls_handshake_failed); }
        SSL_set_fd(ssl, raw->native_handle());
        auto host_str = std::string{host};
        SSL_set_tlsext_host_name(ssl, host_str.c_str());
        if (SSL_connect(ssl) != 1) {
            SSL_free(ssl); SSL_CTX_free(ctx);
            return std::unexpected(cppx::http::net_error::tls_handshake_failed);
        }
        return openssl_tls_stream{std::move(*raw), ctx, ssl};
    }

    auto send(std::span<std::byte const> data) const
        -> std::expected<std::size_t, cppx::http::net_error> {
        auto n = SSL_write(ssl_, data.data(), static_cast<int>(data.size()));
        if (n <= 0) return std::unexpected(cppx::http::net_error::tls_write_failed);
        return static_cast<std::size_t>(n);
    }

    auto recv(std::span<std::byte> buf) const
        -> std::expected<std::size_t, cppx::http::net_error> {
        auto n = SSL_read(ssl_, buf.data(), static_cast<int>(buf.size()));
        if (n <= 0) {
            auto err = SSL_get_error(ssl_, n);
            if (err == SSL_ERROR_ZERO_RETURN)
                return std::unexpected(cppx::http::net_error::connection_closed);
            return std::unexpected(cppx::http::net_error::tls_read_failed);
        }
        return static_cast<std::size_t>(n);
    }

    void close() {
        if (ssl_) { SSL_shutdown(ssl_); SSL_free(ssl_); ssl_ = nullptr; }
        if (ctx_) { SSL_CTX_free(ctx_); ctx_ = nullptr; }
        raw_.close();
    }
};

static_assert(cppx::http::stream_engine<openssl_tls_stream>);

struct openssl_tls {
    using stream = openssl_tls_stream;
    auto wrap(posix_stream raw, std::string_view hostname) const
        -> std::expected<openssl_tls_stream, cppx::http::net_error>
    {
        detail::ensure_openssl();
        auto* ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) return std::unexpected(cppx::http::net_error::tls_handshake_failed);
        SSL_CTX_set_default_verify_paths(ctx);
        auto* ssl = SSL_new(ctx);
        if (!ssl) { SSL_CTX_free(ctx); return std::unexpected(cppx::http::net_error::tls_handshake_failed); }
        SSL_set_fd(ssl, raw.native_handle());
        auto host_str = std::string{hostname};
        SSL_set_tlsext_host_name(ssl, host_str.c_str());
        if (SSL_connect(ssl) != 1) {
            SSL_free(ssl); SSL_CTX_free(ctx);
            return std::unexpected(cppx::http::net_error::tls_handshake_failed);
        }
        return openssl_tls_stream{std::move(raw), ctx, ssl};
    }
};

static_assert(cppx::http::tls_provider<openssl_tls, posix_stream>);
using tls = openssl_tls;

#endif // __APPLE__ / __linux__

// =========================================================================
// Windows — WinSock + SChannel
// =========================================================================

#elif defined(_WIN32)

namespace detail {
struct wsa_init {
    wsa_init() { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
    ~wsa_init() { WSACleanup(); }
};
inline void ensure_wsa() { static wsa_init init; }
} // namespace detail

class win_listener;

class win_stream {
    SOCKET fd_ = INVALID_SOCKET;
    explicit win_stream(SOCKET fd) : fd_{fd} {}
    friend class win_listener;
public:
    win_stream() = default;
    win_stream(win_stream const&) = delete;
    win_stream& operator=(win_stream const&) = delete;
    win_stream(win_stream&& o) noexcept : fd_{std::exchange(o.fd_, INVALID_SOCKET)} {}
    win_stream& operator=(win_stream&& o) noexcept {
        if (this != &o) { close(); fd_ = std::exchange(o.fd_, INVALID_SOCKET); }
        return *this;
    }
    ~win_stream() { close(); }

    static auto connect(std::string_view host, std::uint16_t port)
        -> std::expected<win_stream, cppx::http::net_error> {
        detail::ensure_wsa();
        auto host_str = std::string{host};
        auto port_str = std::to_string(port);
        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        struct addrinfo* result = nullptr;
        if (::getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &result) != 0)
            return std::unexpected(cppx::http::net_error::resolve_failed);
        SOCKET fd = INVALID_SOCKET;
        for (auto* rp = result; rp; rp = rp->ai_next) {
            fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (fd == INVALID_SOCKET) continue;
            if (::connect(fd, rp->ai_addr, static_cast<int>(rp->ai_addrlen)) == 0) break;
            ::closesocket(fd); fd = INVALID_SOCKET;
        }
        ::freeaddrinfo(result);
        if (fd == INVALID_SOCKET) return std::unexpected(cppx::http::net_error::connect_refused);
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char const*>(&one), sizeof(one));
        return win_stream{fd};
    }

    auto send(std::span<std::byte const> data) const
        -> std::expected<std::size_t, cppx::http::net_error> {
        auto n = ::send(fd_, reinterpret_cast<char const*>(data.data()), static_cast<int>(data.size()), 0);
        if (n == SOCKET_ERROR) return std::unexpected(cppx::http::net_error::send_failed);
        return static_cast<std::size_t>(n);
    }

    auto recv(std::span<std::byte> buf) const
        -> std::expected<std::size_t, cppx::http::net_error> {
        auto n = ::recv(fd_, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
        if (n == SOCKET_ERROR) return std::unexpected(cppx::http::net_error::recv_failed);
        if (n == 0) return std::unexpected(cppx::http::net_error::connection_closed);
        return static_cast<std::size_t>(n);
    }

    void close() { if (fd_ != INVALID_SOCKET) { ::closesocket(fd_); fd_ = INVALID_SOCKET; } }
    SOCKET native_handle() const { return fd_; }
};

class win_listener {
    SOCKET fd_ = INVALID_SOCKET;
    explicit win_listener(SOCKET fd) : fd_{fd} {}
public:
    win_listener() = default;
    win_listener(win_listener const&) = delete;
    win_listener& operator=(win_listener const&) = delete;
    win_listener(win_listener&& o) noexcept : fd_{std::exchange(o.fd_, INVALID_SOCKET)} {}
    win_listener& operator=(win_listener&& o) noexcept {
        if (this != &o) { close(); fd_ = std::exchange(o.fd_, INVALID_SOCKET); }
        return *this;
    }
    ~win_listener() { close(); }

    static auto bind(std::string_view host, std::uint16_t port)
        -> std::expected<win_listener, cppx::http::net_error> {
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
            ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char const*>(&one), sizeof(one));
            if (::bind(fd, rp->ai_addr, static_cast<int>(rp->ai_addrlen)) == 0) break;
            ::closesocket(fd); fd = INVALID_SOCKET;
        }
        ::freeaddrinfo(result);
        if (fd == INVALID_SOCKET) return std::unexpected(cppx::http::net_error::bind_failed);
        if (::listen(fd, SOMAXCONN) == SOCKET_ERROR) {
            ::closesocket(fd); return std::unexpected(cppx::http::net_error::bind_failed);
        }
        return win_listener{fd};
    }

    auto accept() const -> std::expected<win_stream, cppx::http::net_error> {
        struct sockaddr_storage addr{};
        int len = sizeof(addr);
        auto client = ::accept(fd_, reinterpret_cast<struct sockaddr*>(&addr), &len);
        if (client == INVALID_SOCKET) return std::unexpected(cppx::http::net_error::accept_failed);
        return win_stream{client};
    }

    void close() { if (fd_ != INVALID_SOCKET) { ::closesocket(fd_); fd_ = INVALID_SOCKET; } }

    auto local_port() const -> std::uint16_t {
        struct sockaddr_storage addr{};
        int len = sizeof(addr);
        if (::getsockname(fd_, reinterpret_cast<struct sockaddr*>(&addr), &len) != 0) return 0;
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

// ---- Windows TLS — SChannel (simplified client-only) ---------------------

class schannel_tls_stream {
    win_stream raw_;
    CredHandle cred_{};
    CtxtHandle ctx_{};
    SecPkgContext_StreamSizes sizes_{};
    bool valid_ = false;
    std::vector<char> recv_buf_;
    std::size_t recv_offset_ = 0;
    std::size_t recv_len_ = 0;

    schannel_tls_stream(win_stream raw, CredHandle cred, CtxtHandle ctx,
                        SecPkgContext_StreamSizes sizes)
        : raw_{std::move(raw)}, cred_{cred}, ctx_{ctx}, sizes_{sizes}, valid_{true} {}

    friend struct schannel_tls;

    static auto do_handshake(win_stream& raw, std::string_view hostname,
                             CredHandle& cred, CtxtHandle& ctx)
        -> std::expected<void, cppx::http::net_error>
    {
        SCHANNEL_CRED sc{};
        sc.dwVersion = SCHANNEL_CRED_VERSION;
        sc.dwFlags = SCH_CRED_AUTO_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS;
        if (AcquireCredentialsHandleA(nullptr, const_cast<char*>(UNISP_NAME_A),
                SECPKG_CRED_OUTBOUND, nullptr, &sc, nullptr, nullptr, &cred, nullptr) != SEC_E_OK)
            return std::unexpected(cppx::http::net_error::tls_handshake_failed);

        auto host_str = std::string{hostname};
        SecBuffer out_buf{0, SECBUFFER_TOKEN, nullptr};
        SecBufferDesc out_desc{SECBUFFER_VERSION, 1, &out_buf};
        DWORD flags = ISC_REQ_REPLAY_DETECT | ISC_REQ_SEQUENCE_DETECT |
                      ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY |
                      ISC_REQ_STREAM;
        DWORD out_flags = 0;

        auto status = InitializeSecurityContextA(&cred, nullptr, host_str.data(),
            flags, 0, 0, nullptr, 0, &ctx, &out_desc, &out_flags, nullptr);

        if (status != SEC_I_CONTINUE_NEEDED) {
            FreeCredentialsHandle(&cred);
            return std::unexpected(cppx::http::net_error::tls_handshake_failed);
        }

        // Send initial token
        if (out_buf.cbBuffer > 0 && out_buf.pvBuffer) {
            auto data = std::span<std::byte const>{
                static_cast<std::byte*>(out_buf.pvBuffer), out_buf.cbBuffer};
            auto total = data.size();
            while (!data.empty()) {
                auto n = raw.send(data);
                if (!n) { FreeContextBuffer(out_buf.pvBuffer); return std::unexpected(n.error()); }
                data = data.subspan(*n);
            }
            FreeContextBuffer(out_buf.pvBuffer);
        }

        // Handshake loop
        std::vector<char> in_data(16384);
        std::size_t in_len = 0;
        while (status == SEC_I_CONTINUE_NEEDED || status == SEC_E_INCOMPLETE_MESSAGE) {
            auto buf_span = std::span<std::byte>{
                reinterpret_cast<std::byte*>(in_data.data() + in_len),
                in_data.size() - in_len};
            auto n = raw.recv(buf_span);
            if (!n) return std::unexpected(cppx::http::net_error::tls_handshake_failed);
            in_len += *n;

            SecBuffer in_bufs[2];
            in_bufs[0] = {static_cast<unsigned long>(in_len), SECBUFFER_TOKEN, in_data.data()};
            in_bufs[1] = {0, SECBUFFER_EMPTY, nullptr};
            SecBufferDesc in_desc{SECBUFFER_VERSION, 2, in_bufs};

            out_buf = {0, SECBUFFER_TOKEN, nullptr};
            out_desc = {SECBUFFER_VERSION, 1, &out_buf};

            status = InitializeSecurityContextA(&cred, &ctx, host_str.data(),
                flags, 0, 0, &in_desc, 0, nullptr, &out_desc, &out_flags, nullptr);

            if (out_buf.cbBuffer > 0 && out_buf.pvBuffer) {
                auto data = std::span<std::byte const>{
                    static_cast<std::byte*>(out_buf.pvBuffer), out_buf.cbBuffer};
                while (!data.empty()) {
                    auto sn = raw.send(data);
                    if (!sn) { FreeContextBuffer(out_buf.pvBuffer); return std::unexpected(sn.error()); }
                    data = data.subspan(*sn);
                }
                FreeContextBuffer(out_buf.pvBuffer);
            }

            // Handle extra data
            if (in_bufs[1].BufferType == SECBUFFER_EXTRA && in_bufs[1].cbBuffer > 0) {
                std::memmove(in_data.data(),
                    in_data.data() + in_len - in_bufs[1].cbBuffer,
                    in_bufs[1].cbBuffer);
                in_len = in_bufs[1].cbBuffer;
            } else {
                in_len = 0;
            }
        }

        if (status != SEC_E_OK) {
            DeleteSecurityContext(&ctx);
            FreeCredentialsHandle(&cred);
            return std::unexpected(cppx::http::net_error::tls_handshake_failed);
        }
        return {};
    }

public:
    schannel_tls_stream() = default;
    schannel_tls_stream(schannel_tls_stream const&) = delete;
    schannel_tls_stream& operator=(schannel_tls_stream const&) = delete;
    schannel_tls_stream(schannel_tls_stream&& o) noexcept
        : raw_{std::move(o.raw_)}, cred_{o.cred_}, ctx_{o.ctx_},
          sizes_{o.sizes_}, valid_{std::exchange(o.valid_, false)},
          recv_buf_{std::move(o.recv_buf_)}, recv_offset_{o.recv_offset_},
          recv_len_{o.recv_len_} {}
    schannel_tls_stream& operator=(schannel_tls_stream&& o) noexcept {
        if (this != &o) {
            close(); raw_ = std::move(o.raw_); cred_ = o.cred_; ctx_ = o.ctx_;
            sizes_ = o.sizes_; valid_ = std::exchange(o.valid_, false);
            recv_buf_ = std::move(o.recv_buf_); recv_offset_ = o.recv_offset_;
            recv_len_ = o.recv_len_;
        }
        return *this;
    }
    ~schannel_tls_stream() { close(); }

    static auto connect(std::string_view host, std::uint16_t port)
        -> std::expected<schannel_tls_stream, cppx::http::net_error> {
        auto raw = win_stream::connect(host, port);
        if (!raw) return std::unexpected(raw.error());
        CredHandle cred{}; CtxtHandle ctx{};
        auto hr = do_handshake(*raw, host, cred, ctx);
        if (!hr) return std::unexpected(hr.error());
        SecPkgContext_StreamSizes sizes{};
        QueryContextAttributes(&ctx, SECPKG_ATTR_STREAM_SIZES, &sizes);
        return schannel_tls_stream{std::move(*raw), cred, ctx, sizes};
    }

    auto send(std::span<std::byte const> data) const
        -> std::expected<std::size_t, cppx::http::net_error> {
        auto total = sizes_.cbHeader + data.size() + sizes_.cbTrailer;
        auto buf = std::vector<char>(total);
        std::memcpy(buf.data() + sizes_.cbHeader, data.data(), data.size());
        SecBuffer bufs[4];
        bufs[0] = {sizes_.cbHeader, SECBUFFER_STREAM_HEADER, buf.data()};
        bufs[1] = {static_cast<unsigned long>(data.size()), SECBUFFER_DATA,
                   buf.data() + sizes_.cbHeader};
        bufs[2] = {sizes_.cbTrailer, SECBUFFER_STREAM_TRAILER,
                   buf.data() + sizes_.cbHeader + data.size()};
        bufs[3] = {0, SECBUFFER_EMPTY, nullptr};
        SecBufferDesc desc{SECBUFFER_VERSION, 4, bufs};
        if (EncryptMessage(const_cast<CtxtHandle*>(&ctx_), 0, &desc, 0) != SEC_E_OK)
            return std::unexpected(cppx::http::net_error::tls_write_failed);
        auto encrypted = std::span<std::byte const>{
            reinterpret_cast<std::byte*>(buf.data()),
            bufs[0].cbBuffer + bufs[1].cbBuffer + bufs[2].cbBuffer};
        auto remaining = encrypted;
        while (!remaining.empty()) {
            auto n = raw_.send(remaining);
            if (!n) return std::unexpected(cppx::http::net_error::tls_write_failed);
            remaining = remaining.subspan(*n);
        }
        return data.size();
    }

    auto recv(std::span<std::byte> buf) const
        -> std::expected<std::size_t, cppx::http::net_error> {
        auto* self = const_cast<schannel_tls_stream*>(this);
        if (self->recv_offset_ < self->recv_len_) {
            auto n = std::min(buf.size(), self->recv_len_ - self->recv_offset_);
            std::memcpy(buf.data(), self->recv_buf_.data() + self->recv_offset_, n);
            self->recv_offset_ += n;
            return n;
        }
        auto enc_buf = std::vector<char>(sizes_.cbMaximumMessage + 256);
        std::size_t enc_len = 0;
        for (;;) {
            auto recv_span = std::span<std::byte>{
                reinterpret_cast<std::byte*>(enc_buf.data() + enc_len),
                enc_buf.size() - enc_len};
            auto n = raw_.recv(recv_span);
            if (!n) return std::unexpected(n.error());
            enc_len += *n;
            SecBuffer bufs[4];
            bufs[0] = {static_cast<unsigned long>(enc_len), SECBUFFER_DATA, enc_buf.data()};
            bufs[1] = {0, SECBUFFER_EMPTY, nullptr};
            bufs[2] = {0, SECBUFFER_EMPTY, nullptr};
            bufs[3] = {0, SECBUFFER_EMPTY, nullptr};
            SecBufferDesc desc{SECBUFFER_VERSION, 4, bufs};
            auto status = DecryptMessage(const_cast<CtxtHandle*>(&ctx_), &desc, 0, nullptr);
            if (status == SEC_E_INCOMPLETE_MESSAGE) continue;
            if (status == SEC_I_CONTEXT_EXPIRED)
                return std::unexpected(cppx::http::net_error::connection_closed);
            if (status != SEC_E_OK)
                return std::unexpected(cppx::http::net_error::tls_read_failed);
            for (int i = 0; i < 4; ++i) {
                if (bufs[i].BufferType == SECBUFFER_DATA && bufs[i].cbBuffer > 0) {
                    auto out_n = std::min(buf.size(), static_cast<std::size_t>(bufs[i].cbBuffer));
                    std::memcpy(buf.data(), bufs[i].pvBuffer, out_n);
                    if (bufs[i].cbBuffer > out_n) {
                        self->recv_buf_.assign(
                            static_cast<char*>(bufs[i].pvBuffer) + out_n,
                            static_cast<char*>(bufs[i].pvBuffer) + bufs[i].cbBuffer);
                        self->recv_offset_ = 0;
                        self->recv_len_ = bufs[i].cbBuffer - out_n;
                    }
                    return out_n;
                }
            }
        }
    }

    void close() {
        if (valid_) {
            DeleteSecurityContext(&ctx_);
            FreeCredentialsHandle(&cred_);
            valid_ = false;
        }
        raw_.close();
    }
};

static_assert(cppx::http::stream_engine<schannel_tls_stream>);

struct schannel_tls {
    using stream = schannel_tls_stream;
    auto wrap(win_stream raw, std::string_view hostname) const
        -> std::expected<schannel_tls_stream, cppx::http::net_error> {
        CredHandle cred{}; CtxtHandle ctx{};
        auto hr = schannel_tls_stream::do_handshake(raw, hostname, cred, ctx);
        if (!hr) return std::unexpected(hr.error());
        SecPkgContext_StreamSizes sizes{};
        QueryContextAttributes(&ctx, SECPKG_ATTR_STREAM_SIZES, &sizes);
        return schannel_tls_stream{std::move(raw), cred, ctx, sizes};
    }
};

static_assert(cppx::http::tls_provider<schannel_tls, win_stream>);
using tls = schannel_tls;

#endif // _WIN32

#if defined(__APPLE__)
#pragma clang diagnostic pop
#endif

// =========================================================================
// Helpers and convenience forwarders
// =========================================================================

template <cppx::http::stream_engine S>
auto send_all(S& s, std::span<std::byte const> data)
    -> std::expected<void, cppx::http::net_error> {
    while (!data.empty()) {
        auto n = s.send(data);
        if (!n) return std::unexpected(n.error());
        data = data.subspan(*n);
    }
    return {};
}

template <cppx::http::stream_engine S>
auto recv_all(S& s, std::vector<std::byte>& out, std::size_t max_size = 64 * 1024 * 1024)
    -> std::expected<void, cppx::http::net_error> {
    auto buf = std::array<std::byte, 8192>{};
    while (out.size() < max_size) {
        auto n = s.recv(buf);
        if (!n) {
            if (n.error() == cppx::http::net_error::connection_closed) return {};
            return std::unexpected(n.error());
        }
        out.insert(out.end(), buf.begin(), buf.begin() + *n);
    }
    return {};
}

// Convenience: HTTPS-capable GET using system stream + TLS.
inline auto get(std::string_view url)
    -> std::expected<cppx::http::response, cppx::http::http_error> {
    return cppx::http::client<stream, tls>{}.get(url);
}

// Convenience: download URL body to a file path.
inline auto download(std::string_view url, std::filesystem::path const& path)
    -> std::expected<void, cppx::http::http_error> {
    auto resp = get(url);
    if (!resp) return std::unexpected(resp.error());
    if (!resp->stat.ok())
        return std::unexpected(cppx::http::http_error::response_parse_failed);
    auto out = std::ofstream{path, std::ios::binary};
    if (!out)
        return std::unexpected(cppx::http::http_error::send_failed);
    out.write(reinterpret_cast<char const*>(resp->body.data()),
              static_cast<std::streamsize>(resp->body.size()));
    return {};
}

} // namespace cppx::http::system
