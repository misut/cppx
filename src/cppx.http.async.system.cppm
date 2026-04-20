// First-party async HTTP facade over cppx.async.system.
//
// POSIX platforms now support HTTPS through the same platform TLS
// backends as cppx.http.system:
//   macOS  - SecureTransport
//   Linux  - OpenSSL
//
// Windows now uses SChannel for first-party HTTPS support.
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

#if defined(_WIN32)
#define SECURITY_WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <security.h>
#include <schannel.h>
#include <sspi.h>
#pragma comment(lib, "secur32.lib")
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

#elif defined(_WIN32)

class schannel_tls_stream {
    struct handshake_owner {
        CredHandle cred{};
        CtxtHandle ctx{};
        bool cred_valid = false;
        bool ctx_valid = false;

        handshake_owner() = default;
        handshake_owner(handshake_owner const&) = delete;
        auto operator=(handshake_owner const&) -> handshake_owner& = delete;

        ~handshake_owner() {
            if (ctx_valid)
                DeleteSecurityContext(&ctx);
            if (cred_valid)
                FreeCredentialsHandle(&cred);
        }
    };

    raw_stream raw_;
    CredHandle cred_{};
    CtxtHandle ctx_{};
    SecPkgContext_StreamSizes sizes_{};
    bool valid_ = false;
    std::vector<char> recv_buf_{};
    std::size_t recv_offset_ = 0;
    std::vector<char> enc_buf_{};
    std::size_t enc_len_ = 0;
    std::vector<char> send_buf_{};

    static auto record_buffer_size(SecPkgContext_StreamSizes const& sizes)
        -> std::size_t
    {
        return static_cast<std::size_t>(sizes.cbHeader)
            + static_cast<std::size_t>(sizes.cbMaximumMessage)
            + static_cast<std::size_t>(sizes.cbTrailer);
    }

    static auto complete_auth_if_needed(
        CtxtHandle& ctx,
        SecBufferDesc& out_desc,
        SECURITY_STATUS status)
        -> std::expected<SECURITY_STATUS, cppx::net::net_error>
    {
        if (status != SEC_I_COMPLETE_NEEDED
            && status != SEC_I_COMPLETE_AND_CONTINUE) {
            return status;
        }

        if (CompleteAuthToken(&ctx, &out_desc) != SEC_E_OK) {
            return std::unexpected(
                cppx::net::net_error::tls_handshake_failed);
        }

        if (status == SEC_I_COMPLETE_NEEDED)
            return SECURITY_STATUS{SEC_E_OK};
        return SECURITY_STATUS{SEC_I_CONTINUE_NEEDED};
    }

    static auto send_token(raw_stream& raw, SecBuffer& token)
        -> cppx::async::task<std::expected<void, cppx::net::net_error>>
    {
        if (token.cbBuffer == 0 || !token.pvBuffer) {
            co_return std::expected<void, cppx::net::net_error>{};
        }

        auto remaining = std::span<std::byte const>{
            reinterpret_cast<std::byte const*>(token.pvBuffer),
            static_cast<std::size_t>(token.cbBuffer),
        };
        while (!remaining.empty()) {
            auto sent = co_await raw.send(remaining);
            if (!sent) {
                FreeContextBuffer(token.pvBuffer);
                token.pvBuffer = nullptr;
                token.cbBuffer = 0;
                co_return std::unexpected(sent.error());
            }
            remaining = remaining.subspan(*sent);
        }

        FreeContextBuffer(token.pvBuffer);
        token.pvBuffer = nullptr;
        token.cbBuffer = 0;
        co_return std::expected<void, cppx::net::net_error>{};
    }

    static auto from_raw(raw_stream raw, std::string_view hostname)
        -> cppx::async::task<
            std::expected<schannel_tls_stream, cppx::net::net_error>>
    {
        auto handles = handshake_owner{};
        auto sc = SCHANNEL_CRED{};
        sc.dwVersion = SCHANNEL_CRED_VERSION;
        sc.dwFlags = SCH_CRED_AUTO_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS;
        if (AcquireCredentialsHandleA(
                nullptr,
                const_cast<char*>(UNISP_NAME_A),
                SECPKG_CRED_OUTBOUND,
                nullptr,
                &sc,
                nullptr,
                nullptr,
                &handles.cred,
                nullptr)
            != SEC_E_OK) {
            co_return std::unexpected(
                cppx::net::net_error::tls_handshake_failed);
        }
        handles.cred_valid = true;

        auto host = std::string{hostname};
        auto const flags = DWORD{
            ISC_REQ_REPLAY_DETECT
            | ISC_REQ_SEQUENCE_DETECT
            | ISC_REQ_CONFIDENTIALITY
            | ISC_REQ_ALLOCATE_MEMORY
            | ISC_REQ_STREAM
        };
        auto in_data = std::vector<char>(16 * 1024);
        auto in_len = std::size_t{0};
        auto status = SECURITY_STATUS{};
        auto first_call = true;

        while (true) {
            auto const needs_more_input =
                !first_call
                && (status == SEC_E_INCOMPLETE_MESSAGE
                    || (status == SEC_I_CONTINUE_NEEDED && in_len == 0));
            if (needs_more_input) {
                if (in_len == in_data.size())
                    in_data.resize(in_data.size() * 2);

                auto recv_span = std::span<std::byte>{
                    reinterpret_cast<std::byte*>(in_data.data() + in_len),
                    in_data.size() - in_len,
                };
                auto received = co_await raw.recv(recv_span);
                if (!received) {
                    co_return std::unexpected(
                        status == SEC_E_INCOMPLETE_MESSAGE
                            ? cppx::net::net_error::tls_handshake_failed
                            : received.error());
                }
                in_len += *received;
            }

            auto out_buffer = SecBuffer{0, SECBUFFER_TOKEN, nullptr};
            auto out_desc = SecBufferDesc{
                SECBUFFER_VERSION,
                1,
                &out_buffer,
            };
            auto out_flags = DWORD{0};

            if (first_call) {
                status = InitializeSecurityContextA(
                    &handles.cred,
                    nullptr,
                    host.data(),
                    flags,
                    0,
                    0,
                    nullptr,
                    0,
                    &handles.ctx,
                    &out_desc,
                    &out_flags,
                    nullptr);
            } else {
                auto in_buffers = std::array{
                    SecBuffer{
                        static_cast<unsigned long>(in_len),
                        SECBUFFER_TOKEN,
                        in_data.data(),
                    },
                    SecBuffer{0, SECBUFFER_EMPTY, nullptr},
                };
                auto in_desc = SecBufferDesc{
                    SECBUFFER_VERSION,
                    static_cast<unsigned long>(in_buffers.size()),
                    in_buffers.data(),
                };

                status = InitializeSecurityContextA(
                    &handles.cred,
                    &handles.ctx,
                    host.data(),
                    flags,
                    0,
                    0,
                    &in_desc,
                    0,
                    &handles.ctx,
                    &out_desc,
                    &out_flags,
                    nullptr);
                auto const extra = in_buffers[1].BufferType == SECBUFFER_EXTRA
                    ? static_cast<std::size_t>(in_buffers[1].cbBuffer)
                    : std::size_t{0};
                if (extra > 0) {
                    std::memmove(
                        in_data.data(),
                        in_data.data() + in_len - extra,
                        extra);
                    in_len = extra;
                } else if (status != SEC_E_INCOMPLETE_MESSAGE) {
                    in_len = 0;
                }
            }

            if (first_call
                && (status == SEC_E_OK
                    || status == SEC_I_CONTINUE_NEEDED
                    || status == SEC_I_COMPLETE_NEEDED
                    || status == SEC_I_COMPLETE_AND_CONTINUE)) {
                handles.ctx_valid = true;
            }

            auto normalized = complete_auth_if_needed(
                handles.ctx, out_desc, status);
            if (!normalized) {
                if (out_buffer.pvBuffer)
                    FreeContextBuffer(out_buffer.pvBuffer);
                co_return std::unexpected(normalized.error());
            }
            status = *normalized;

            auto token_sent = co_await send_token(raw, out_buffer);
            if (!token_sent)
                co_return std::unexpected(token_sent.error());

            if (status == SEC_E_OK)
                break;
            if (status != SEC_I_CONTINUE_NEEDED
                && status != SEC_E_INCOMPLETE_MESSAGE) {
                co_return std::unexpected(
                    cppx::net::net_error::tls_handshake_failed);
            }

            if (status == SEC_E_INCOMPLETE_MESSAGE && in_len == in_data.size())
                in_data.resize(in_data.size() * 2);

            first_call = false;
        }

        auto sizes = SecPkgContext_StreamSizes{};
        if (QueryContextAttributes(
                &handles.ctx,
                SECPKG_ATTR_STREAM_SIZES,
                &sizes)
            != SEC_E_OK) {
            co_return std::unexpected(
                cppx::net::net_error::tls_handshake_failed);
        }
        auto stream = schannel_tls_stream{
            std::move(raw),
            handles.cred,
            handles.ctx,
            sizes,
            std::vector<char>{in_data.begin(), in_data.begin() + in_len},
        };
        handles.cred_valid = false;
        handles.ctx_valid = false;
        co_return std::move(stream);
    }

    schannel_tls_stream(raw_stream raw,
                        CredHandle cred,
                        CtxtHandle ctx,
                        SecPkgContext_StreamSizes sizes,
                        std::vector<char> encrypted_backlog)
        : raw_{std::move(raw)},
          cred_{cred},
          ctx_{ctx},
          sizes_{sizes},
          valid_{true},
          enc_buf_(std::max(
              record_buffer_size(sizes),
              std::max<std::size_t>(encrypted_backlog.size(), 16 * 1024))),
          enc_len_{encrypted_backlog.size()},
          send_buf_(record_buffer_size(sizes))
    {
        if (!encrypted_backlog.empty()) {
            std::memcpy(
                enc_buf_.data(),
                encrypted_backlog.data(),
                encrypted_backlog.size());
        }
    }

public:
    schannel_tls_stream() = default;
    schannel_tls_stream(schannel_tls_stream const&) = delete;
    auto operator=(schannel_tls_stream const&) -> schannel_tls_stream& = delete;

    schannel_tls_stream(schannel_tls_stream&& other) noexcept
        : raw_{std::move(other.raw_)},
          cred_{other.cred_},
          ctx_{other.ctx_},
          sizes_{other.sizes_},
          valid_{std::exchange(other.valid_, false)},
          recv_buf_{std::move(other.recv_buf_)},
          recv_offset_{other.recv_offset_},
          enc_buf_{std::move(other.enc_buf_)},
          enc_len_{other.enc_len_},
          send_buf_{std::move(other.send_buf_)}
    {
    }

    auto operator=(schannel_tls_stream&& other) noexcept
        -> schannel_tls_stream& {
        if (this != &other) {
            close();
            raw_ = std::move(other.raw_);
            cred_ = other.cred_;
            ctx_ = other.ctx_;
            sizes_ = other.sizes_;
            valid_ = std::exchange(other.valid_, false);
            recv_buf_ = std::move(other.recv_buf_);
            recv_offset_ = other.recv_offset_;
            enc_buf_ = std::move(other.enc_buf_);
            enc_len_ = other.enc_len_;
            send_buf_ = std::move(other.send_buf_);
        }
        return *this;
    }

    ~schannel_tls_stream() {
        close();
    }

    static auto connect(std::string_view host, std::uint16_t port)
        -> cppx::async::task<
            std::expected<schannel_tls_stream, cppx::net::net_error>>
    {
        auto raw = co_await raw_stream::connect(host, port);
        if (!raw)
            co_return std::unexpected(raw.error());
        co_return co_await from_raw(std::move(*raw), host);
    }

    auto send(std::span<std::byte const> data)
        -> cppx::async::task<std::expected<std::size_t, cppx::net::net_error>>
    {
        if (data.empty())
            co_return std::size_t{0};

        auto total_sent = std::size_t{0};
        while (total_sent < data.size()) {
            auto const chunk_size = std::min(
                data.size() - total_sent,
                static_cast<std::size_t>(sizes_.cbMaximumMessage));
            auto const chunk = data.subspan(total_sent, chunk_size);
            auto const total_size = static_cast<std::size_t>(sizes_.cbHeader)
                + chunk.size()
                + static_cast<std::size_t>(sizes_.cbTrailer);
            send_buf_.resize(total_size);
            std::memcpy(
                send_buf_.data() + sizes_.cbHeader,
                chunk.data(),
                chunk.size());

            auto buffers = std::array{
                SecBuffer{
                    sizes_.cbHeader,
                    SECBUFFER_STREAM_HEADER,
                    send_buf_.data(),
                },
                SecBuffer{
                    static_cast<unsigned long>(chunk.size()),
                    SECBUFFER_DATA,
                    send_buf_.data() + sizes_.cbHeader,
                },
                SecBuffer{
                    sizes_.cbTrailer,
                    SECBUFFER_STREAM_TRAILER,
                    send_buf_.data() + sizes_.cbHeader + chunk.size(),
                },
                SecBuffer{0, SECBUFFER_EMPTY, nullptr},
            };
            auto desc = SecBufferDesc{
                SECBUFFER_VERSION,
                static_cast<unsigned long>(buffers.size()),
                buffers.data(),
            };

            auto const status = EncryptMessage(&ctx_, 0, &desc, 0);
            if (status == SEC_I_CONTEXT_EXPIRED)
                co_return std::unexpected(cppx::net::net_error::connection_closed);
            if (status != SEC_E_OK)
                co_return std::unexpected(
                    cppx::net::net_error::tls_write_failed);

            auto encrypted = std::span<std::byte const>{
                reinterpret_cast<std::byte const*>(send_buf_.data()),
                static_cast<std::size_t>(buffers[0].cbBuffer)
                    + static_cast<std::size_t>(buffers[1].cbBuffer)
                    + static_cast<std::size_t>(buffers[2].cbBuffer),
            };
            while (!encrypted.empty()) {
                auto sent = co_await raw_.send(encrypted);
                if (!sent)
                    co_return std::unexpected(
                        sent.error() == cppx::net::net_error::connection_closed
                            ? cppx::net::net_error::tls_write_failed
                            : sent.error());
                encrypted = encrypted.subspan(*sent);
            }

            total_sent += chunk.size();
        }

        co_return total_sent;
    }

    auto recv(std::span<std::byte> buffer)
        -> cppx::async::task<std::expected<std::size_t, cppx::net::net_error>>
    {
        if (buffer.empty())
            co_return std::size_t{0};

        if (recv_offset_ < recv_buf_.size()) {
            auto const available = recv_buf_.size() - recv_offset_;
            auto const copied = std::min(buffer.size(), available);
            std::memcpy(
                buffer.data(),
                recv_buf_.data() + recv_offset_,
                copied);
            recv_offset_ += copied;
            if (recv_offset_ == recv_buf_.size()) {
                recv_buf_.clear();
                recv_offset_ = 0;
            }
            co_return copied;
        }
        recv_buf_.clear();
        recv_offset_ = 0;

        auto ensure_capacity = [&](std::size_t extra) {
            if (enc_buf_.size() - enc_len_ >= extra)
                return;

            auto next = std::max(
                enc_buf_.size() * 2,
                enc_len_ + extra);
            next = std::max(next, record_buffer_size(sizes_));
            enc_buf_.resize(next);
        };

        for (;;) {
            if (enc_len_ == 0) {
                ensure_capacity(1);
                auto recv_span = std::span<std::byte>{
                    reinterpret_cast<std::byte*>(enc_buf_.data()),
                    enc_buf_.size(),
                };
                auto received = co_await raw_.recv(recv_span);
                if (!received)
                    co_return std::unexpected(received.error());
                enc_len_ = *received;
            }

            auto buffers = std::array{
                SecBuffer{
                    static_cast<unsigned long>(enc_len_),
                    SECBUFFER_DATA,
                    enc_buf_.data(),
                },
                SecBuffer{0, SECBUFFER_EMPTY, nullptr},
                SecBuffer{0, SECBUFFER_EMPTY, nullptr},
                SecBuffer{0, SECBUFFER_EMPTY, nullptr},
            };
            auto desc = SecBufferDesc{
                SECBUFFER_VERSION,
                static_cast<unsigned long>(buffers.size()),
                buffers.data(),
            };

            auto const status = DecryptMessage(&ctx_, &desc, 0, nullptr);
            if (status == SEC_E_INCOMPLETE_MESSAGE) {
                ensure_capacity(1);
                auto recv_span = std::span<std::byte>{
                    reinterpret_cast<std::byte*>(enc_buf_.data() + enc_len_),
                    enc_buf_.size() - enc_len_,
                };
                auto received = co_await raw_.recv(recv_span);
                if (!received) {
                    co_return std::unexpected(
                        received.error() == cppx::net::net_error::connection_closed
                            ? cppx::net::net_error::tls_read_failed
                            : received.error());
                }
                enc_len_ += *received;
                continue;
            }
            if (status == SEC_I_CONTEXT_EXPIRED)
                co_return std::unexpected(cppx::net::net_error::connection_closed);
            // Post-handshake renegotiation stays unsupported in this pass.
            if (status == SEC_I_RENEGOTIATE)
                co_return std::unexpected(cppx::net::net_error::tls_read_failed);
            if (status != SEC_E_OK)
                co_return std::unexpected(cppx::net::net_error::tls_read_failed);

            auto extra = std::size_t{0};
            auto data_buffer = static_cast<SecBuffer*>(nullptr);
            for (auto& sb : buffers) {
                if (!data_buffer
                    && sb.BufferType == SECBUFFER_DATA
                    && sb.cbBuffer > 0) {
                    data_buffer = &sb;
                }
                if (sb.BufferType == SECBUFFER_EXTRA)
                    extra = sb.cbBuffer;
            }

            if (data_buffer) {
                auto const copied = std::min(
                    buffer.size(),
                    static_cast<std::size_t>(data_buffer->cbBuffer));
                std::memcpy(buffer.data(), data_buffer->pvBuffer, copied);
                if (copied < data_buffer->cbBuffer) {
                    auto* begin = static_cast<char*>(data_buffer->pvBuffer)
                        + copied;
                    auto* end = static_cast<char*>(data_buffer->pvBuffer)
                        + data_buffer->cbBuffer;
                    recv_buf_.assign(begin, end);
                    recv_offset_ = 0;
                } else {
                    recv_buf_.clear();
                    recv_offset_ = 0;
                }
                if (extra > 0) {
                    std::memmove(
                        enc_buf_.data(),
                        enc_buf_.data() + enc_len_ - extra,
                        extra);
                    enc_len_ = extra;
                } else {
                    enc_len_ = 0;
                }
                co_return copied;
            }

            if (extra > 0) {
                std::memmove(
                    enc_buf_.data(),
                    enc_buf_.data() + enc_len_ - extra,
                    extra);
                enc_len_ = extra;
            } else {
                enc_len_ = 0;
            }

            if (enc_len_ == 0) {
                ensure_capacity(1);
                auto recv_span = std::span<std::byte>{
                    reinterpret_cast<std::byte*>(enc_buf_.data()),
                    enc_buf_.size(),
                };
                auto received = co_await raw_.recv(recv_span);
                if (!received)
                    co_return std::unexpected(received.error());
                enc_len_ = *received;
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

    friend struct schannel_tls;
};

struct schannel_tls {
    using stream = schannel_tls_stream;

    auto wrap(raw_stream raw, std::string_view hostname) const
        -> cppx::async::task<
            std::expected<schannel_tls_stream, cppx::net::net_error>>
    {
        co_return co_await schannel_tls_stream::from_raw(
            std::move(raw), hostname);
    }
};

using system_tls = schannel_tls;

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
