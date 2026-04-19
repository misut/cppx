// Pure transport boundary for network-capable modules. Owns shared
// network error types plus sync stream/listener/TLS concepts and null
// helpers that higher-level protocols can build on.

export module cppx.net;
import std;

export namespace cppx::net {

enum class net_error {
    resolve_failed, connect_refused, bind_failed, accept_failed,
    send_failed, recv_failed, connection_closed, timeout,
    tls_handshake_failed, tls_read_failed, tls_write_failed,
};

inline constexpr auto to_string(net_error e) -> std::string_view {
    switch (e) {
    case net_error::resolve_failed:        return "resolve_failed";
    case net_error::connect_refused:       return "connect_refused";
    case net_error::bind_failed:           return "bind_failed";
    case net_error::accept_failed:         return "accept_failed";
    case net_error::send_failed:           return "send_failed";
    case net_error::recv_failed:           return "recv_failed";
    case net_error::connection_closed:     return "connection_closed";
    case net_error::timeout:               return "timeout";
    case net_error::tls_handshake_failed:  return "tls_handshake_failed";
    case net_error::tls_read_failed:       return "tls_read_failed";
    case net_error::tls_write_failed:      return "tls_write_failed";
    }
    return "unknown";
}

template <class T>
concept stream_engine = requires(T s,
                                 std::string_view host, std::uint16_t port,
                                 std::span<std::byte const> out,
                                 std::span<std::byte> in) {
    { T::connect(host, port) }
        -> std::same_as<std::expected<T, net_error>>;
    { s.send(out) } -> std::same_as<std::expected<std::size_t, net_error>>;
    { s.recv(in)  } -> std::same_as<std::expected<std::size_t, net_error>>;
    { s.close()   } -> std::same_as<void>;
};

template <class T, class Stream>
concept listener_engine = stream_engine<Stream> &&
    requires(T l, std::string_view host, std::uint16_t port) {
    { T::bind(host, port) }
        -> std::same_as<std::expected<T, net_error>>;
    { l.accept() }
        -> std::same_as<std::expected<Stream, net_error>>;
    { l.close() } -> std::same_as<void>;
};

template <class T, class RawStream>
concept tls_provider = stream_engine<RawStream> &&
    requires(T t, RawStream raw, std::string_view hostname) {
    { t.wrap(std::move(raw), hostname) }
        -> std::same_as<std::expected<typename T::stream, net_error>>;
};

struct null_stream {
    static auto connect(std::string_view, std::uint16_t)
        -> std::expected<null_stream, net_error> {
        return std::unexpected(net_error::connect_refused);
    }
    auto send(std::span<std::byte const>) const
        -> std::expected<std::size_t, net_error> {
        return std::unexpected(net_error::send_failed);
    }
    auto recv(std::span<std::byte>) const
        -> std::expected<std::size_t, net_error> {
        return std::unexpected(net_error::recv_failed);
    }
    void close() const {}
};
static_assert(stream_engine<null_stream>);

struct null_listener {
    static auto bind(std::string_view, std::uint16_t)
        -> std::expected<null_listener, net_error> {
        return std::unexpected(net_error::bind_failed);
    }
    auto accept() const -> std::expected<null_stream, net_error> {
        return std::unexpected(net_error::accept_failed);
    }
    void close() const {}
};
static_assert(listener_engine<null_listener, null_stream>);

struct null_tls {
    using stream = null_stream;
    auto wrap(null_stream, std::string_view) const
        -> std::expected<null_stream, net_error> {
        return std::unexpected(net_error::tls_handshake_failed);
    }
};
static_assert(tls_provider<null_tls, null_stream>);

} // namespace cppx::net
