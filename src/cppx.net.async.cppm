// Async transport boundary built on cppx.async tasks. Keeps async
// stream/listener/TLS concepts separate from protocol modules so the
// coroutine dependency stays explicit and opt-in.

export module cppx.net.async;
import cppx.async;
import cppx.net;
import std;

export namespace cppx::net::async {

template <class T>
concept stream_engine = requires(T s,
                                 std::string_view host, std::uint16_t port,
                                 std::span<std::byte const> out,
                                 std::span<std::byte> in) {
    { T::connect(host, port) }
        -> std::same_as<cppx::async::task<std::expected<T, cppx::net::net_error>>>;
    { s.send(out) }
        -> std::same_as<cppx::async::task<std::expected<std::size_t, cppx::net::net_error>>>;
    { s.recv(in)  }
        -> std::same_as<cppx::async::task<std::expected<std::size_t, cppx::net::net_error>>>;
    { s.close() } -> std::same_as<void>;
};

template <class T, class Stream>
concept listener_engine = stream_engine<Stream> &&
    requires(T l, std::string_view host, std::uint16_t port) {
    { T::bind(host, port) }
        -> std::same_as<cppx::async::task<std::expected<T, cppx::net::net_error>>>;
    { l.accept() }
        -> std::same_as<cppx::async::task<std::expected<Stream, cppx::net::net_error>>>;
    { l.close() } -> std::same_as<void>;
};

template <class T, class RawStream>
concept tls_provider = stream_engine<RawStream> &&
    requires(T t, RawStream raw, std::string_view hostname) {
    { t.wrap(std::move(raw), hostname) }
        -> std::same_as<cppx::async::task<
               std::expected<typename T::stream, cppx::net::net_error>>>;
};

struct null_stream {
    static auto connect(std::string_view, std::uint16_t)
        -> cppx::async::task<std::expected<null_stream, cppx::net::net_error>> {
        co_return std::unexpected(cppx::net::net_error::connect_refused);
    }
    auto send(std::span<std::byte const>) const
        -> cppx::async::task<std::expected<std::size_t, cppx::net::net_error>> {
        co_return std::unexpected(cppx::net::net_error::send_failed);
    }
    auto recv(std::span<std::byte>) const
        -> cppx::async::task<std::expected<std::size_t, cppx::net::net_error>> {
        co_return std::unexpected(cppx::net::net_error::recv_failed);
    }
    void close() const {}
};
static_assert(stream_engine<null_stream>);

struct null_listener {
    static auto bind(std::string_view, std::uint16_t)
        -> cppx::async::task<std::expected<null_listener, cppx::net::net_error>> {
        co_return std::unexpected(cppx::net::net_error::bind_failed);
    }
    auto accept() const
        -> cppx::async::task<std::expected<null_stream, cppx::net::net_error>> {
        co_return std::unexpected(cppx::net::net_error::accept_failed);
    }
    void close() const {}
};
static_assert(listener_engine<null_listener, null_stream>);

struct null_tls {
    using stream = null_stream;
    auto wrap(null_stream, std::string_view) const
        -> cppx::async::task<std::expected<null_stream, cppx::net::net_error>> {
        co_return std::unexpected(cppx::net::net_error::tls_handshake_failed);
    }
};
static_assert(tls_provider<null_tls, null_stream>);

} // namespace cppx::net::async
