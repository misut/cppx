// Platform socket engines and TLS providers for cppx.http. This is the
// only impure module in the HTTP stack — it wraps platform sockets and
// TLS behind the concepts defined in cppx.http.
//
// TLS backends:
//   macOS  — Security.framework (SecureTransport)
//   Linux  — system OpenSSL (libssl/libcrypto)
//   Windows — SChannel (SSPI) for low-level streams, WinHTTP for the
//             preferred system HTTP client facade

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
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#include <security.h>
#include <schannel.h>
#include <sspi.h>
#pragma comment(lib, "secur32.lib")
#endif

export module cppx.http.system;

// The HTTP system module is platform-specific (POSIX sockets / WinSock /
// TLS). On wasm32-wasi there is no socket API, so the module compiles as
// an empty stub — callers should not import it on that target.
#if !defined(__wasi__)
import cppx.bytes;
import std;
import cppx.http;
import cppx.http.client;
import cppx.http.server;

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

// ---- Windows TLS — SChannel (legacy low-level client-only path) ----------
// The preferred first-party HTTP entrypoint on Windows is the WinHTTP-
// backed system::client/get/download facade below. This transport remains
// available for low-level stream/TLS seams and compatibility only.

class schannel_tls_stream {
    win_stream raw_;
    CredHandle cred_{};
    CtxtHandle ctx_{};
    SecPkgContext_StreamSizes sizes_{};
    bool valid_ = false;
    std::vector<char> recv_buf_;
    std::size_t recv_offset_ = 0;
    std::size_t recv_len_ = 0;
    std::vector<char> enc_buf_;
    std::size_t enc_len_ = 0;

    schannel_tls_stream(win_stream raw, CredHandle cred, CtxtHandle ctx,
                        SecPkgContext_StreamSizes sizes)
        : raw_{std::move(raw)}, cred_{cred}, ctx_{ctx}, sizes_{sizes}, valid_{true},
          enc_buf_(static_cast<std::size_t>(sizes.cbHeader) +
                   static_cast<std::size_t>(sizes.cbMaximumMessage) +
                   static_cast<std::size_t>(sizes.cbTrailer)) {}

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
          recv_len_{o.recv_len_}, enc_buf_{std::move(o.enc_buf_)},
          enc_len_{o.enc_len_} {}
    schannel_tls_stream& operator=(schannel_tls_stream&& o) noexcept {
        if (this != &o) {
            close(); raw_ = std::move(o.raw_); cred_ = o.cred_; ctx_ = o.ctx_;
            sizes_ = o.sizes_; valid_ = std::exchange(o.valid_, false);
            recv_buf_ = std::move(o.recv_buf_); recv_offset_ = o.recv_offset_;
            recv_len_ = o.recv_len_; enc_buf_ = std::move(o.enc_buf_);
            enc_len_ = o.enc_len_;
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
        for (;;) {
            if (self->enc_len_ == self->enc_buf_.size()) {
                auto next = std::max<std::size_t>(
                    self->enc_buf_.size() * 2,
                    static_cast<std::size_t>(sizes_.cbHeader) +
                    static_cast<std::size_t>(sizes_.cbMaximumMessage) +
                    static_cast<std::size_t>(sizes_.cbTrailer));
                self->enc_buf_.resize(next);
            }

            if (self->enc_len_ == 0) {
                auto recv_span = std::span<std::byte>{
                    reinterpret_cast<std::byte*>(self->enc_buf_.data()),
                    self->enc_buf_.size()};
                auto n = raw_.recv(recv_span);
                if (!n) return std::unexpected(n.error());
                self->enc_len_ = *n;
            }

            SecBuffer bufs[4];
            bufs[0] = {
                static_cast<unsigned long>(self->enc_len_),
                SECBUFFER_DATA,
                self->enc_buf_.data()
            };
            bufs[1] = {0, SECBUFFER_EMPTY, nullptr};
            bufs[2] = {0, SECBUFFER_EMPTY, nullptr};
            bufs[3] = {0, SECBUFFER_EMPTY, nullptr};
            SecBufferDesc desc{SECBUFFER_VERSION, 4, bufs};
            auto status = DecryptMessage(const_cast<CtxtHandle*>(&ctx_), &desc, 0, nullptr);
            if (status == SEC_E_INCOMPLETE_MESSAGE) {
                auto recv_span = std::span<std::byte>{
                    reinterpret_cast<std::byte*>(self->enc_buf_.data() + self->enc_len_),
                    self->enc_buf_.size() - self->enc_len_};
                auto n = raw_.recv(recv_span);
                if (!n) return std::unexpected(n.error());
                self->enc_len_ += *n;
                continue;
            }
            if (status == SEC_I_CONTEXT_EXPIRED)
                return std::unexpected(cppx::http::net_error::connection_closed);
            if (status != SEC_E_OK)
                return std::unexpected(cppx::http::net_error::tls_read_failed);

            // SChannel can decrypt one TLS record while leaving additional
            // encrypted records in SECBUFFER_EXTRA. Preserve them for the
            // next recv so higher layers do not lose response bytes.
            std::size_t extra = 0;
            for (auto const& sb : bufs)
                if (sb.BufferType == SECBUFFER_EXTRA)
                    extra = sb.cbBuffer;
            if (extra > 0) {
                std::memmove(
                    self->enc_buf_.data(),
                    self->enc_buf_.data() + self->enc_len_ - extra,
                    extra);
                self->enc_len_ = extra;
            } else {
                self->enc_len_ = 0;
            }

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

            if (extra == 0) {
                auto recv_span = std::span<std::byte>{
                    reinterpret_cast<std::byte*>(self->enc_buf_.data()),
                    self->enc_buf_.size()};
                auto n = raw_.recv(recv_span);
                if (!n) return std::unexpected(n.error());
                self->enc_len_ = *n;
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
auto send_all(S& s, cppx::bytes::bytes_view data)
    -> std::expected<void, cppx::http::net_error> {
    while (!data.empty()) {
        auto n = s.send(std::span{data.data(), data.size()});
        if (!n) return std::unexpected(n.error());
        data = data.subview(*n);
    }
    return {};
}

template <cppx::http::stream_engine S>
auto recv_all(S& s,
              cppx::bytes::byte_buffer& out,
              std::size_t max_size = 64 * 1024 * 1024)
    -> std::expected<void, cppx::http::net_error> {
    auto buf = std::array<std::byte, 8192>{};
    while (out.size() < max_size) {
        auto n = s.recv(buf);
        if (!n) {
            if (n.error() == cppx::http::net_error::connection_closed) return {};
            return std::unexpected(n.error());
        }
        out.append(cppx::bytes::bytes_view{buf.data(), *n});
    }
    return {};
}

#if defined(_WIN32)
namespace detail::winhttp {

inline constexpr auto default_get_body_limit = std::size_t{64 * 1024 * 1024};
inline constexpr auto winhttp_redirect_limit = DWORD{5};

class winhttp_handle {
    HINTERNET handle_ = nullptr;
public:
    winhttp_handle() = default;
    explicit winhttp_handle(HINTERNET handle) : handle_{handle} {}
    winhttp_handle(winhttp_handle const&) = delete;
    auto operator=(winhttp_handle const&) -> winhttp_handle& = delete;
    winhttp_handle(winhttp_handle&& other) noexcept
        : handle_{std::exchange(other.handle_, nullptr)} {}
    auto operator=(winhttp_handle&& other) noexcept -> winhttp_handle& {
        if (this != &other) {
            close();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }
    ~winhttp_handle() { close(); }

    void close() {
        if (handle_) {
            WinHttpCloseHandle(handle_);
            handle_ = nullptr;
        }
    }

    auto get() const -> HINTERNET { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }
};

struct winhttp_request_context {
    winhttp_handle session;
    winhttp_handle connection;
    winhttp_handle request;
};

inline auto map_winhttp_error(DWORD err) -> cppx::http::http_error {
    switch (err) {
    case ERROR_WINHTTP_INVALID_URL:
    case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:
        return cppx::http::http_error::url_parse_failed;
    case ERROR_WINHTTP_TIMEOUT:
        return cppx::http::http_error::timeout;
    case ERROR_WINHTTP_SECURE_FAILURE:
    case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED:
        return cppx::http::http_error::tls_failed;
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
    case ERROR_WINHTTP_CANNOT_CONNECT:
    case ERROR_WINHTTP_CONNECTION_ERROR:
    case ERROR_WINHTTP_AUTODETECTION_FAILED:
        return cppx::http::http_error::connection_failed;
    case ERROR_WINHTTP_RESEND_REQUEST:
        return cppx::http::http_error::redirect_limit;
    default:
        return cppx::http::http_error::response_parse_failed;
    }
}

inline auto utf8_to_wide(std::string_view value)
    -> std::expected<std::wstring, cppx::http::http_error>
{
    if (value.empty()) return std::wstring{};
    auto len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                   value.data(),
                                   static_cast<int>(value.size()),
                                   nullptr, 0);
    if (len <= 0)
        return std::unexpected(cppx::http::http_error::response_parse_failed);
    auto out = std::wstring(static_cast<std::size_t>(len), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            value.data(),
                            static_cast<int>(value.size()),
                            out.data(), len) != len) {
        return std::unexpected(cppx::http::http_error::response_parse_failed);
    }
    return out;
}

inline auto wide_to_utf8(std::wstring_view value)
    -> std::expected<std::string, cppx::http::http_error>
{
    if (value.empty()) return std::string{};
    auto len = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                   static_cast<int>(value.size()),
                                   nullptr, 0, nullptr, nullptr);
    if (len <= 0)
        return std::unexpected(cppx::http::http_error::response_parse_failed);
    auto out = std::string(static_cast<std::size_t>(len), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.data(),
                            static_cast<int>(value.size()),
                            out.data(), len, nullptr, nullptr) != len) {
        return std::unexpected(cppx::http::http_error::response_parse_failed);
    }
    return out;
}

inline auto build_target_path(cppx::http::url const& target) -> std::string {
    auto path = target.path.empty() ? std::string{"/"} : target.path;
    if (!target.query.empty())
        path += std::format("?{}", target.query);
    return path;
}

inline auto query_status_code(HINTERNET request)
    -> std::expected<std::uint16_t, cppx::http::http_error>
{
    auto status = DWORD{};
    auto size = DWORD{sizeof(status)};
    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        return std::unexpected(map_winhttp_error(GetLastError()));
    }
    return static_cast<std::uint16_t>(status);
}

inline auto query_raw_headers(HINTERNET request)
    -> std::expected<std::wstring, cppx::http::http_error>
{
    auto size = DWORD{0};
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             WINHTTP_NO_OUTPUT_BUFFER, &size,
                             WINHTTP_NO_HEADER_INDEX)) {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            return std::unexpected(map_winhttp_error(GetLastError()));
    }

    auto wide_count = static_cast<std::size_t>(size / sizeof(wchar_t));
    auto raw = std::wstring(wide_count, L'\0');
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             raw.data(), &size,
                             WINHTTP_NO_HEADER_INDEX)) {
        return std::unexpected(map_winhttp_error(GetLastError()));
    }
    if (!raw.empty() && raw.back() == L'\0')
        raw.pop_back();
    return raw;
}

inline auto append_headers_from_raw(std::wstring_view raw,
                                    cppx::http::headers& hdrs)
    -> std::expected<void, cppx::http::http_error>
{
    auto raw_utf8 = wide_to_utf8(raw);
    if (!raw_utf8) return std::unexpected(raw_utf8.error());

    auto block = std::string_view{*raw_utf8};
    auto first_eol = block.find("\r\n");
    if (first_eol == std::string_view::npos)
        return {};
    block.remove_prefix(first_eol + 2);

    while (!block.empty()) {
        auto eol = block.find("\r\n");
        if (eol == std::string_view::npos)
            break;
        auto line = block.substr(0, eol);
        block.remove_prefix(eol + 2);
        if (line.empty())
            break;

        auto colon = line.find(':');
        if (colon == std::string_view::npos)
            return std::unexpected(cppx::http::http_error::response_parse_failed);
        auto key = line.substr(0, colon);
        auto value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ')
            value.remove_prefix(1);
        hdrs.append(key, value);
    }
    return {};
}

inline auto add_request_headers(HINTERNET request,
                                cppx::http::headers const& extra)
    -> std::expected<void, cppx::http::http_error>
{
    for (auto const& [name, value] : extra) {
        auto line_utf8 = std::format("{}: {}", name, value);
        auto line = utf8_to_wide(line_utf8);
        if (!line) return std::unexpected(line.error());
        if (!WinHttpAddRequestHeaders(request, line->c_str(),
                                      static_cast<DWORD>(line->size()),
                                      WINHTTP_ADDREQ_FLAG_ADD)) {
            return std::unexpected(map_winhttp_error(GetLastError()));
        }
    }
    return {};
}

inline auto configure_request(HINTERNET request)
    -> std::expected<void, cppx::http::http_error>
{
    auto redirect_limit = winhttp_redirect_limit;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS,
                          &redirect_limit, sizeof(redirect_limit))) {
        return std::unexpected(map_winhttp_error(GetLastError()));
    }

    auto redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY,
                          &redirect_policy, sizeof(redirect_policy))) {
        return std::unexpected(map_winhttp_error(GetLastError()));
    }

    auto header_limit =
        static_cast<DWORD>(cppx::http::default_response_header_limit);
    if (!WinHttpSetOption(request, WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE,
                          &header_limit, sizeof(header_limit))) {
        return std::unexpected(map_winhttp_error(GetLastError()));
    }

    return {};
}

inline auto send_and_receive(HINTERNET request)
    -> std::expected<void, cppx::http::http_error>
{
    for (auto attempts = 0; attempts < 8; ++attempts) {
        if (!WinHttpSendRequest(request,
                                WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            auto err = GetLastError();
            if (err == ERROR_WINHTTP_RESEND_REQUEST)
                continue;
            return std::unexpected(map_winhttp_error(err));
        }
        if (!WinHttpReceiveResponse(request, nullptr)) {
            auto err = GetLastError();
            if (err == ERROR_WINHTTP_RESEND_REQUEST)
                continue;
            return std::unexpected(map_winhttp_error(err));
        }
        return {};
    }
    return std::unexpected(cppx::http::http_error::redirect_limit);
}

inline auto make_response_metadata(HINTERNET request)
    -> std::expected<cppx::http::response, cppx::http::http_error>
{
    auto code = query_status_code(request);
    if (!code) return std::unexpected(code.error());

    auto raw_headers = query_raw_headers(request);
    if (!raw_headers) return std::unexpected(raw_headers.error());

    auto resp = cppx::http::response{};
    resp.stat = cppx::http::status{*code};
    auto parsed = append_headers_from_raw(*raw_headers, resp.hdrs);
    if (!parsed) return std::unexpected(parsed.error());
    return resp;
}

inline auto build_request_context(cppx::http::url const& target,
                                  cppx::http::headers const& extra)
    -> std::expected<winhttp_request_context, cppx::http::http_error>
{
    auto session = winhttp_handle{
        WinHttpOpen(L"cppx.http.system/1.0",
                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                    WINHTTP_NO_PROXY_NAME,
                    WINHTTP_NO_PROXY_BYPASS, 0)
    };
    if (!session)
        return std::unexpected(map_winhttp_error(GetLastError()));

    auto host = utf8_to_wide(target.host);
    if (!host) return std::unexpected(host.error());
    auto connection = winhttp_handle{
        WinHttpConnect(session.get(), host->c_str(), target.effective_port(), 0)
    };
    if (!connection)
        return std::unexpected(map_winhttp_error(GetLastError()));

    auto path = utf8_to_wide(build_target_path(target));
    if (!path) return std::unexpected(path.error());
    auto request = winhttp_handle{
        WinHttpOpenRequest(connection.get(), L"GET", path->c_str(),
                           nullptr, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                           target.is_tls() ? WINHTTP_FLAG_SECURE : 0)
    };
    if (!request)
        return std::unexpected(map_winhttp_error(GetLastError()));

    auto configured = configure_request(request.get());
    if (!configured) return std::unexpected(configured.error());

    auto added = add_request_headers(request.get(), extra);
    if (!added) return std::unexpected(added.error());

    return winhttp_request_context{
        .session = std::move(session),
        .connection = std::move(connection),
        .request = std::move(request),
    };
}

inline auto read_body(HINTERNET request,
                      cppx::bytes::byte_buffer& body,
                      std::size_t max_body)
    -> std::expected<void, cppx::http::http_error>
{
    auto buf = std::array<std::byte, 8192>{};
    auto total = std::size_t{0};
    for (;;) {
        auto read = DWORD{0};
        if (!WinHttpReadData(request, buf.data(),
                             static_cast<DWORD>(buf.size()), &read)) {
            return std::unexpected(map_winhttp_error(GetLastError()));
        }
        if (read == 0)
            return {};
        if (total + read > max_body)
            return std::unexpected(cppx::http::http_error::body_too_large);
        body.append(cppx::bytes::bytes_view{buf.data(), read});
        total += read;
    }
}

inline auto finalize_download(std::filesystem::path const& temp_path,
                              std::filesystem::path const& final_path)
    -> std::expected<void, cppx::http::http_error>
{
    auto ec = std::error_code{};
    std::filesystem::remove(final_path, ec);
    std::filesystem::rename(temp_path, final_path, ec);
    if (ec)
        return std::unexpected(cppx::http::http_error::send_failed);
    return {};
}

inline auto stream_body_to_file(HINTERNET request,
                                std::filesystem::path const& path,
                                std::size_t max_body)
    -> std::expected<void, cppx::http::http_error>
{
    auto temp_path = path;
    temp_path += ".part";
    auto ec = std::error_code{};
    std::filesystem::remove(temp_path, ec);

    auto out = std::ofstream{temp_path, std::ios::binary | std::ios::trunc};
    if (!out)
        return std::unexpected(cppx::http::http_error::send_failed);

    auto cleanup = [&] {
        out.close();
        auto remove_ec = std::error_code{};
        std::filesystem::remove(temp_path, remove_ec);
    };

    auto buf = std::array<std::byte, 8192>{};
    auto total = std::size_t{0};
    for (;;) {
        auto read = DWORD{0};
        if (!WinHttpReadData(request, buf.data(),
                             static_cast<DWORD>(buf.size()), &read)) {
            cleanup();
            return std::unexpected(map_winhttp_error(GetLastError()));
        }
        if (read == 0)
            break;
        if (total + read > max_body) {
            cleanup();
            return std::unexpected(cppx::http::http_error::body_too_large);
        }
        out.write(reinterpret_cast<char const*>(buf.data()),
                  static_cast<std::streamsize>(read));
        if (!out) {
            cleanup();
            return std::unexpected(cppx::http::http_error::send_failed);
        }
        total += read;
    }

    out.close();
    if (!out) {
        cleanup();
        return std::unexpected(cppx::http::http_error::send_failed);
    }

    auto finalized = finalize_download(temp_path, path);
    if (!finalized) {
        cleanup();
        return std::unexpected(finalized.error());
    }
    return {};
}

inline auto get(std::string_view url, cppx::http::headers extra)
    -> std::expected<cppx::http::response, cppx::http::http_error>
{
    auto target = cppx::http::url::parse(url);
    if (!target)
        return std::unexpected(cppx::http::http_error::url_parse_failed);

    auto ctx = build_request_context(*target, extra);
    if (!ctx)
        return std::unexpected(ctx.error());

    auto sent = send_and_receive(ctx->request.get());
    if (!sent)
        return std::unexpected(sent.error());

    auto resp = make_response_metadata(ctx->request.get());
    if (!resp)
        return std::unexpected(resp.error());

    auto read = read_body(
        ctx->request.get(),
        resp->body,
        default_get_body_limit);
    if (!read)
        return std::unexpected(read.error());

    return resp;
}

inline auto download_to(std::string_view url,
                        std::filesystem::path const& path,
                        cppx::http::headers extra,
                        std::size_t max_body)
    -> std::expected<cppx::http::response, cppx::http::http_error>
{
    auto target = cppx::http::url::parse(url);
    if (!target)
        return std::unexpected(cppx::http::http_error::url_parse_failed);

    auto ctx = build_request_context(*target, extra);
    if (!ctx)
        return std::unexpected(ctx.error());

    auto sent = send_and_receive(ctx->request.get());
    if (!sent)
        return std::unexpected(sent.error());

    auto resp = make_response_metadata(ctx->request.get());
    if (!resp)
        return std::unexpected(resp.error());

    if (!resp->stat.ok())
        return resp;

    auto streamed = stream_body_to_file(ctx->request.get(), path, max_body);
    if (!streamed)
        return std::unexpected(streamed.error());

    resp->body = {};
    return resp;
}

} // namespace detail::winhttp
#endif // _WIN32

// Preferred system HTTP facade. Platform-specific HTTP/TLS details stay
// behind this interface so first-party callers do not assemble transports.
class client {
public:
    auto get(std::string_view url, cppx::http::headers extra = {})
        -> std::expected<cppx::http::response, cppx::http::http_error>
    {
#if defined(_WIN32)
        return detail::winhttp::get(url, std::move(extra));
#else
        return cppx::http::client<stream, tls>{}.get(url, std::move(extra));
#endif
    }

    auto download_to(std::string_view url,
                     std::filesystem::path const& path,
                     cppx::http::headers extra = {},
                     std::size_t max_body = cppx::http::default_download_body_limit)
        -> std::expected<cppx::http::response, cppx::http::http_error>
    {
#if defined(_WIN32)
        return detail::winhttp::download_to(
            url,
            path,
            std::move(extra),
            max_body);
#else
        return cppx::http::client<stream, tls>{}.download_to(
            url, path, std::move(extra), max_body);
#endif
    }
};

// Convenience: HTTPS-capable GET using the preferred system HTTP facade.
// Follows redirects automatically.
inline auto get(std::string_view url)
    -> std::expected<cppx::http::response, cppx::http::http_error> {
    return client{}.get(url);
}

inline auto get(std::string_view url, cppx::http::headers extra)
    -> std::expected<cppx::http::response, cppx::http::http_error> {
    return client{}.get(url, std::move(extra));
}

// Convenience: download URL body to a file path.
// Follows redirects and streams to disk without a default body cap.
inline auto download(std::string_view url, std::filesystem::path const& path)
    -> std::expected<void, cppx::http::http_error> {
    auto resp = client{}.download_to(url, path);
    if (!resp) return std::unexpected(resp.error());
    if (!resp->stat.ok())
        return std::unexpected(cppx::http::http_error::response_parse_failed);
    return {};
}

inline auto download(std::string_view url, std::filesystem::path const& path,
                     cppx::http::headers extra)
    -> std::expected<void, cppx::http::http_error> {
    auto resp = client{}.download_to(url, path, std::move(extra));
    if (!resp) return std::unexpected(resp.error());
    if (!resp->stat.ok())
        return std::unexpected(cppx::http::http_error::response_parse_failed);
    return {};
}

// Convenience: start an HTTP server serving static files.
inline auto serve_static(std::filesystem::path root, std::uint16_t port)
    -> std::expected<void, cppx::http::net_error> {
    cppx::http::server<listener, stream> srv;
    srv.serve_static("/", std::move(root));
    return srv.run("0.0.0.0", port);
}

} // namespace cppx::http::system
#endif // !defined(__wasi__)
