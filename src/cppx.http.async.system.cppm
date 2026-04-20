// First-party async HTTP facade over cppx.async.system.
//
// POSIX platforms now support HTTPS through the same platform TLS
// backends as cppx.http.system:
//   macOS  - SecureTransport
//   Linux  - OpenSSL
//
// Windows remains plain-HTTP-only in this pass and still reports
// tls_failed for https:// requests.
//
// On wasm32-wasi there is no async socket backend, so this module
// compiles as an empty stub and callers should not use it there.

module;

#if defined(__APPLE__) || defined(__linux__)
#include <cerrno>
#include <sys/socket.h>
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

export module cppx.http.async.system;

#if !defined(__wasi__)
import cppx.async;
import cppx.async.system;
import cppx.bytes;
import cppx.http;
import cppx.http.async;
import cppx.net;
import std;

export namespace cppx::http::async::system {

namespace detail {

using raw_stream = cppx::async::system::async_stream;
using native_socket = cppx::async::system::native_socket;

enum class wait_kind {
    read,
    write,
};

struct socket_waiter {
    native_socket fd;
    wait_kind kind;

    auto await_ready() const noexcept -> bool {
        return false;
    }

    auto await_suspend(std::coroutine_handle<> handle) const -> bool {
        if (!cppx::async::system::current_loop)
            return false;

        if (kind == wait_kind::write)
            cppx::async::system::current_loop->watch_writable(fd, handle);
        else
            cppx::async::system::current_loop->watch_readable(fd, handle);
        return true;
    }

    void await_resume() const noexcept {}
};

inline auto wait_readable(native_socket fd) -> socket_waiter {
    return {fd, wait_kind::read};
}

inline auto wait_writable(native_socket fd) -> socket_waiter {
    return {fd, wait_kind::write};
}

inline auto wait_for(wait_kind kind, native_socket fd) -> socket_waiter {
    return {fd, kind};
}

struct no_tls {
    using stream = raw_stream;

    auto wrap(stream raw, std::string_view) const
        -> cppx::async::task<std::expected<stream, cppx::net::net_error>>
    {
        raw.close();
        co_return std::unexpected(cppx::net::net_error::tls_handshake_failed);
    }
};

inline auto parse_url(std::string_view raw)
    -> std::expected<cppx::http::url, cppx::http::http_error>
{
    auto parsed = cppx::http::url::parse(raw);
    if (!parsed)
        return std::unexpected(cppx::http::http_error::url_parse_failed);
#if defined(_WIN32)
    if (parsed->is_tls())
        return std::unexpected(cppx::http::http_error::tls_failed);
#endif
    return std::move(*parsed);
}

#if defined(__APPLE__)

struct apple_tls_state {
    std::unique_ptr<raw_stream> raw;
    wait_kind blocked_on = wait_kind::read;
};

class apple_tls_stream {
    std::unique_ptr<apple_tls_state> state_;
    SSLContextRef ctx_ = nullptr;

    apple_tls_stream(std::unique_ptr<apple_tls_state> state, SSLContextRef ctx)
        : state_{std::move(state)}, ctx_{ctx} {}

    static auto read_cb(SSLConnectionRef conn, void* data, size_t* len)
        -> OSStatus
    {
        auto* state = static_cast<apple_tls_state*>(const_cast<void*>(conn));
        state->blocked_on = wait_kind::read;

        auto const n = ::recv(state->raw->native_handle(), data, *len, 0);
        if (n > 0) {
            auto const requested = *len;
            *len = static_cast<size_t>(n);
            return *len == requested ? noErr : errSSLWouldBlock;
        }
        if (n == 0) {
            *len = 0;
            return errSSLClosedGraceful;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            *len = 0;
            return errSSLWouldBlock;
        }

        *len = 0;
        return errSSLClosedAbort;
    }

    static auto write_cb(SSLConnectionRef conn, void const* data, size_t* len)
        -> OSStatus
    {
        auto* state = static_cast<apple_tls_state*>(const_cast<void*>(conn));
        state->blocked_on = wait_kind::write;

        auto const n = ::send(state->raw->native_handle(), data, *len, 0);
        if (n >= 0) {
            auto const requested = *len;
            *len = static_cast<size_t>(n);
            return *len == requested ? noErr : errSSLWouldBlock;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            *len = 0;
            return errSSLWouldBlock;
        }

        *len = 0;
        return errSSLClosedAbort;
    }

    auto configure(std::string_view hostname)
        -> std::expected<void, cppx::net::net_error>
    {
        if (!ctx_)
            return std::unexpected(cppx::net::net_error::tls_handshake_failed);
        if (SSLSetIOFuncs(ctx_, read_cb, write_cb) != noErr)
            return std::unexpected(cppx::net::net_error::tls_handshake_failed);
        if (SSLSetConnection(ctx_, state_.get()) != noErr)
            return std::unexpected(cppx::net::net_error::tls_handshake_failed);

        auto host = std::string{hostname};
        if (SSLSetPeerDomainName(ctx_, host.c_str(), host.size()) != noErr)
            return std::unexpected(cppx::net::net_error::tls_handshake_failed);
        if (SSLSetProtocolVersionMin(ctx_, kTLSProtocol12) != noErr)
            return std::unexpected(cppx::net::net_error::tls_handshake_failed);
        if (SSLSetProtocolVersionMax(ctx_, kTLSProtocol12) != noErr)
            return std::unexpected(cppx::net::net_error::tls_handshake_failed);
        return {};
    }

    auto finish_handshake()
        -> cppx::async::task<std::expected<void, cppx::net::net_error>>
    {
        while (true) {
            auto const status = SSLHandshake(ctx_);
            if (status == noErr)
                co_return std::expected<void, cppx::net::net_error>{};
            if (status == errSSLWouldBlock) {
                co_await wait_for(
                    state_->blocked_on, state_->raw->native_handle());
                continue;
            }
            co_return std::unexpected(
                cppx::net::net_error::tls_handshake_failed);
        }
    }

    static auto from_raw(raw_stream raw, std::string_view hostname)
        -> cppx::async::task<
            std::expected<apple_tls_stream, cppx::net::net_error>>
    {
        auto ctx = SSLCreateContext(nullptr, kSSLClientSide, kSSLStreamType);
        if (!ctx)
            co_return std::unexpected(
                cppx::net::net_error::tls_handshake_failed);

        auto state = std::make_unique<apple_tls_state>();
        state->raw = std::make_unique<raw_stream>(std::move(raw));

        auto stream = apple_tls_stream{std::move(state), ctx};
        auto configured = stream.configure(hostname);
        if (!configured)
            co_return std::unexpected(configured.error());

        auto handshaken = co_await stream.finish_handshake();
        if (!handshaken)
            co_return std::unexpected(handshaken.error());
        co_return std::move(stream);
    }

public:
    apple_tls_stream() = default;
    apple_tls_stream(apple_tls_stream const&) = delete;
    auto operator=(apple_tls_stream const&) -> apple_tls_stream& = delete;

    apple_tls_stream(apple_tls_stream&& other) noexcept
        : state_{std::move(other.state_)},
          ctx_{std::exchange(other.ctx_, nullptr)}
    {
    }

    auto operator=(apple_tls_stream&& other) noexcept -> apple_tls_stream& {
        if (this != &other) {
            close();
            state_ = std::move(other.state_);
            ctx_ = std::exchange(other.ctx_, nullptr);
        }
        return *this;
    }

    ~apple_tls_stream() {
        close();
    }

    static auto connect(std::string_view host, std::uint16_t port)
        -> cppx::async::task<
            std::expected<apple_tls_stream, cppx::net::net_error>>
    {
        auto raw = co_await raw_stream::connect(host, port);
        if (!raw)
            co_return std::unexpected(raw.error());
        co_return co_await from_raw(std::move(*raw), host);
    }

    auto send(std::span<std::byte const> data)
        -> cppx::async::task<std::expected<std::size_t, cppx::net::net_error>>
    {
        while (true) {
            auto written = size_t{0};
            auto const status =
                SSLWrite(ctx_, data.data(), data.size(), &written);
            if (status == noErr || (status == errSSLWouldBlock && written > 0))
                co_return written;
            if (status == errSSLWouldBlock) {
                co_await wait_for(
                    state_->blocked_on, state_->raw->native_handle());
                continue;
            }
            co_return std::unexpected(cppx::net::net_error::tls_write_failed);
        }
    }

    auto recv(std::span<std::byte> buffer)
        -> cppx::async::task<std::expected<std::size_t, cppx::net::net_error>>
    {
        while (true) {
            auto read = size_t{0};
            auto const status =
                SSLRead(ctx_, buffer.data(), buffer.size(), &read);
            if (status == noErr || (status == errSSLWouldBlock && read > 0))
                co_return read;
            if (status == errSSLWouldBlock) {
                co_await wait_for(
                    state_->blocked_on, state_->raw->native_handle());
                continue;
            }
            if (status == errSSLClosedGraceful
                || (status == errSSLClosedNoNotify && read == 0)) {
                co_return std::unexpected(
                    cppx::net::net_error::connection_closed);
            }
            co_return std::unexpected(cppx::net::net_error::tls_read_failed);
        }
    }

    void close() {
        if (ctx_) {
            CFRelease(ctx_);
            ctx_ = nullptr;
        }
        if (state_ && state_->raw)
            state_->raw->close();
    }

    friend struct apple_tls;
};

struct apple_tls {
    using stream = apple_tls_stream;

    auto wrap(raw_stream raw, std::string_view hostname) const
        -> cppx::async::task<
            std::expected<apple_tls_stream, cppx::net::net_error>>
    {
        co_return co_await apple_tls_stream::from_raw(
            std::move(raw), hostname);
    }
};

using system_tls = apple_tls;

#elif defined(__linux__)

struct openssl_init {
    openssl_init() {
        SSL_library_init();
        SSL_load_error_strings();
    }
};

inline void ensure_openssl() {
    static auto init = openssl_init{};
    (void)init;
}

class openssl_tls_stream {
    raw_stream raw_;
    SSL_CTX* ctx_ = nullptr;
    SSL* ssl_ = nullptr;

    openssl_tls_stream(raw_stream raw, SSL_CTX* ctx, SSL* ssl)
        : raw_{std::move(raw)}, ctx_{ctx}, ssl_{ssl} {}

    auto finish_handshake()
        -> cppx::async::task<std::expected<void, cppx::net::net_error>>
    {
        while (true) {
            auto const rc = SSL_connect(ssl_);
            if (rc == 1)
                co_return std::expected<void, cppx::net::net_error>{};

            auto const err = SSL_get_error(ssl_, rc);
            if (err == SSL_ERROR_WANT_READ) {
                co_await wait_readable(raw_.native_handle());
                continue;
            }
            if (err == SSL_ERROR_WANT_WRITE) {
                co_await wait_writable(raw_.native_handle());
                continue;
            }

            co_return std::unexpected(
                cppx::net::net_error::tls_handshake_failed);
        }
    }

    static auto from_raw(raw_stream raw, std::string_view hostname)
        -> cppx::async::task<
            std::expected<openssl_tls_stream, cppx::net::net_error>>
    {
        ensure_openssl();
        auto* ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx)
            co_return std::unexpected(
                cppx::net::net_error::tls_handshake_failed);

        SSL_CTX_set_default_verify_paths(ctx);
        auto* ssl = SSL_new(ctx);
        if (!ssl) {
            SSL_CTX_free(ctx);
            co_return std::unexpected(
                cppx::net::net_error::tls_handshake_failed);
        }

        SSL_set_fd(ssl, raw.native_handle());
        auto host = std::string{hostname};
        SSL_set_tlsext_host_name(ssl, host.c_str());

        auto stream = openssl_tls_stream{std::move(raw), ctx, ssl};
        auto handshaken = co_await stream.finish_handshake();
        if (!handshaken)
            co_return std::unexpected(handshaken.error());
        co_return std::move(stream);
    }

public:
    openssl_tls_stream() = default;
    openssl_tls_stream(openssl_tls_stream const&) = delete;
    auto operator=(openssl_tls_stream const&) -> openssl_tls_stream& = delete;

    openssl_tls_stream(openssl_tls_stream&& other) noexcept
        : raw_{std::move(other.raw_)},
          ctx_{std::exchange(other.ctx_, nullptr)},
          ssl_{std::exchange(other.ssl_, nullptr)}
    {
    }

    auto operator=(openssl_tls_stream&& other) noexcept
        -> openssl_tls_stream& {
        if (this != &other) {
            close();
            raw_ = std::move(other.raw_);
            ctx_ = std::exchange(other.ctx_, nullptr);
            ssl_ = std::exchange(other.ssl_, nullptr);
        }
        return *this;
    }

    ~openssl_tls_stream() {
        close();
    }

    static auto connect(std::string_view host, std::uint16_t port)
        -> cppx::async::task<
            std::expected<openssl_tls_stream, cppx::net::net_error>>
    {
        auto raw = co_await raw_stream::connect(host, port);
        if (!raw)
            co_return std::unexpected(raw.error());
        co_return co_await from_raw(std::move(*raw), host);
    }

    auto send(std::span<std::byte const> data)
        -> cppx::async::task<std::expected<std::size_t, cppx::net::net_error>>
    {
        while (true) {
            auto const rc =
                SSL_write(ssl_, data.data(), static_cast<int>(data.size()));
            if (rc > 0)
                co_return static_cast<std::size_t>(rc);

            auto const err = SSL_get_error(ssl_, rc);
            if (err == SSL_ERROR_WANT_READ) {
                co_await wait_readable(raw_.native_handle());
                continue;
            }
            if (err == SSL_ERROR_WANT_WRITE) {
                co_await wait_writable(raw_.native_handle());
                continue;
            }

            co_return std::unexpected(cppx::net::net_error::tls_write_failed);
        }
    }

    auto recv(std::span<std::byte> buffer)
        -> cppx::async::task<std::expected<std::size_t, cppx::net::net_error>>
    {
        while (true) {
            auto const rc = SSL_read(
                ssl_, buffer.data(), static_cast<int>(buffer.size()));
            if (rc > 0)
                co_return static_cast<std::size_t>(rc);

            auto const err = SSL_get_error(ssl_, rc);
            if (err == SSL_ERROR_ZERO_RETURN)
                co_return std::unexpected(
                    cppx::net::net_error::connection_closed);
            if (err == SSL_ERROR_WANT_READ) {
                co_await wait_readable(raw_.native_handle());
                continue;
            }
            if (err == SSL_ERROR_WANT_WRITE) {
                co_await wait_writable(raw_.native_handle());
                continue;
            }

            co_return std::unexpected(cppx::net::net_error::tls_read_failed);
        }
    }

    void close() {
        if (ssl_) {
            SSL_shutdown(ssl_);
            SSL_free(ssl_);
            ssl_ = nullptr;
        }
        if (ctx_) {
            SSL_CTX_free(ctx_);
            ctx_ = nullptr;
        }
        raw_.close();
    }

    friend struct openssl_tls;
};

struct openssl_tls {
    using stream = openssl_tls_stream;

    auto wrap(raw_stream raw, std::string_view hostname) const
        -> cppx::async::task<
            std::expected<openssl_tls_stream, cppx::net::net_error>>
    {
        co_return co_await openssl_tls_stream::from_raw(
            std::move(raw), hostname);
    }
};

using system_tls = openssl_tls;

#else

using system_tls = no_tls;

#endif

} // namespace detail

class client {
public:
    auto request(cppx::http::request const& req, int max_redirects = 5,
                 std::size_t max_body = 64 * 1024 * 1024)
        -> cppx::async::task<
            std::expected<cppx::http::response, cppx::http::http_error>>
    {
#if defined(_WIN32)
        if (req.target.is_tls())
            co_return std::unexpected(cppx::http::http_error::tls_failed);
#endif

        auto inner = cppx::http::async::client<
            cppx::async::system::async_stream,
            detail::system_tls>{};
        co_return co_await inner.request(req, max_redirects, max_body);
    }

    auto get(std::string_view url, cppx::http::headers extra = {})
        -> cppx::async::task<
            std::expected<cppx::http::response, cppx::http::http_error>>
    {
        auto parsed = detail::parse_url(url);
        if (!parsed)
            co_return std::unexpected(parsed.error());

        cppx::http::request req;
        req.verb = cppx::http::method::GET;
        req.target = std::move(*parsed);
        req.hdrs = std::move(extra);
        co_return co_await request(req);
    }

    auto head(std::string_view url)
        -> cppx::async::task<
            std::expected<cppx::http::response, cppx::http::http_error>>
    {
        auto parsed = detail::parse_url(url);
        if (!parsed)
            co_return std::unexpected(parsed.error());

        cppx::http::request req;
        req.verb = cppx::http::method::HEAD;
        req.target = std::move(*parsed);
        co_return co_await request(req);
    }

    auto post(std::string_view url,
              std::string_view content_type,
              cppx::bytes::byte_buffer body)
        -> cppx::async::task<
            std::expected<cppx::http::response, cppx::http::http_error>>
    {
        auto parsed = detail::parse_url(url);
        if (!parsed)
            co_return std::unexpected(parsed.error());

        cppx::http::request req;
        req.verb = cppx::http::method::POST;
        req.target = std::move(*parsed);
        req.hdrs.set("content-type", content_type);
        req.body = std::move(body);
        co_return co_await request(req);
    }

    auto download_to(std::string_view url,
                     std::filesystem::path const& path,
                     cppx::http::headers extra = {},
                     std::size_t max_body
                     = cppx::http::default_download_body_limit)
        -> cppx::async::task<
            std::expected<cppx::http::response, cppx::http::http_error>>
    {
        auto parsed = detail::parse_url(url);
        if (!parsed)
            co_return std::unexpected(parsed.error());

        auto inner = cppx::http::async::client<
            cppx::async::system::async_stream,
            detail::system_tls>{};
        co_return co_await inner.download_to(
            parsed->to_string(), path, std::move(extra), max_body);
    }
};

inline auto get(std::string_view url)
    -> cppx::async::task<
        std::expected<cppx::http::response, cppx::http::http_error>>
{
    co_return co_await client{}.get(url);
}

inline auto get(std::string_view url, cppx::http::headers extra)
    -> cppx::async::task<
        std::expected<cppx::http::response, cppx::http::http_error>>
{
    co_return co_await client{}.get(url, std::move(extra));
}

inline auto download(std::string_view url, std::filesystem::path const& path)
    -> cppx::async::task<std::expected<void, cppx::http::http_error>>
{
    auto resp = co_await client{}.download_to(url, path);
    if (!resp)
        co_return std::unexpected(resp.error());
    if (!resp->stat.ok())
        co_return std::unexpected(cppx::http::http_error::response_parse_failed);
    co_return std::expected<void, cppx::http::http_error>{};
}

inline auto download(std::string_view url,
                     std::filesystem::path const& path,
                     cppx::http::headers extra)
    -> cppx::async::task<std::expected<void, cppx::http::http_error>>
{
    auto resp = co_await client{}.download_to(url, path, std::move(extra));
    if (!resp)
        co_return std::unexpected(resp.error());
    if (!resp->stat.ok())
        co_return std::unexpected(cppx::http::http_error::response_parse_failed);
    co_return std::expected<void, cppx::http::http_error>{};
}

} // namespace cppx::http::async::system

#endif // !defined(__wasi__)

#if defined(__APPLE__)
#pragma clang diagnostic pop
#endif
