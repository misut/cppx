// Pure HTTP module. Types, RFC 9112 parser/serializer, engine concepts,
// and test doubles. No I/O — all networking goes through injected
// capabilities (see cppx.http.system for the impure edge).

export module cppx.http;
import cppx.async;
import std;

export namespace cppx::http {

// ---- error types ---------------------------------------------------------

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

enum class parse_error {
    incomplete,
    invalid_request_line,
    invalid_status_line,
    invalid_header,
    header_too_large,
    body_too_large,
    bad_chunk_encoding,
    bad_content_length,
};

inline constexpr auto to_string(parse_error e) -> std::string_view {
    switch (e) {
    case parse_error::incomplete:            return "incomplete";
    case parse_error::invalid_request_line:  return "invalid_request_line";
    case parse_error::invalid_status_line:   return "invalid_status_line";
    case parse_error::invalid_header:        return "invalid_header";
    case parse_error::header_too_large:      return "header_too_large";
    case parse_error::body_too_large:        return "body_too_large";
    case parse_error::bad_chunk_encoding:    return "bad_chunk_encoding";
    case parse_error::bad_content_length:    return "bad_content_length";
    }
    return "unknown";
}

enum class http_error {
    url_parse_failed, connection_failed, tls_failed,
    send_failed, response_parse_failed, timeout,
    redirect_limit, body_too_large,
};

inline constexpr auto to_string(http_error e) -> std::string_view {
    switch (e) {
    case http_error::url_parse_failed:     return "url_parse_failed";
    case http_error::connection_failed:    return "connection_failed";
    case http_error::tls_failed:           return "tls_failed";
    case http_error::send_failed:          return "send_failed";
    case http_error::response_parse_failed:return "response_parse_failed";
    case http_error::timeout:              return "timeout";
    case http_error::redirect_limit:       return "redirect_limit";
    case http_error::body_too_large:       return "body_too_large";
    }
    return "unknown";
}

// ---- HTTP method ---------------------------------------------------------

enum class method { GET, HEAD, POST, PUT, DELETE_, PATCH, OPTIONS };

inline constexpr auto to_string(method m) -> std::string_view {
    switch (m) {
    case method::GET:     return "GET";
    case method::HEAD:    return "HEAD";
    case method::POST:    return "POST";
    case method::PUT:     return "PUT";
    case method::DELETE_: return "DELETE";
    case method::PATCH:   return "PATCH";
    case method::OPTIONS: return "OPTIONS";
    }
    return "UNKNOWN";
}

inline auto method_from_string(std::string_view s) -> std::optional<method> {
    if (s == "GET")     return method::GET;
    if (s == "HEAD")    return method::HEAD;
    if (s == "POST")    return method::POST;
    if (s == "PUT")     return method::PUT;
    if (s == "DELETE")  return method::DELETE_;
    if (s == "PATCH")   return method::PATCH;
    if (s == "OPTIONS") return method::OPTIONS;
    return std::nullopt;
}

// ---- HTTP status ---------------------------------------------------------

struct status {
    std::uint16_t code;

    constexpr bool ok() const { return code >= 200 && code < 300; }

    constexpr auto reason() const -> std::string_view {
        switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default:  return "Unknown";
        }
    }
};

// ---- headers -------------------------------------------------------------

// Case-insensitive header storage. Keys are lowercased on insertion.
class headers {
    std::vector<std::pair<std::string, std::string>> entries_;

    static auto lower(std::string_view s) -> std::string {
        auto result = std::string{s};
        for (auto& c : result)
            if (c >= 'A' && c <= 'Z') c += 32;
        return result;
    }
public:
    void set(std::string_view name, std::string_view value) {
        auto key = lower(name);
        for (auto& [k, v] : entries_) {
            if (k == key) { v = std::string{value}; return; }
        }
        entries_.emplace_back(std::move(key), std::string{value});
    }

    void append(std::string_view name, std::string_view value) {
        entries_.emplace_back(lower(name), std::string{value});
    }

    auto get(std::string_view name) const -> std::optional<std::string_view> {
        auto key = lower(name);
        for (auto const& [k, v] : entries_)
            if (k == key) return v;
        return std::nullopt;
    }

    bool contains(std::string_view name) const {
        return get(name).has_value();
    }

    auto begin() const { return entries_.begin(); }
    auto end() const { return entries_.end(); }
    auto size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }
};

// ---- URL -----------------------------------------------------------------

struct url {
    std::string scheme;
    std::string host;
    std::uint16_t port = 0;
    std::string path;
    std::string query;

    auto effective_port() const -> std::uint16_t {
        if (port != 0) return port;
        return (scheme == "https") ? std::uint16_t{443} : std::uint16_t{80};
    }

    bool is_tls() const { return scheme == "https"; }

    static auto parse(std::string_view raw) -> std::expected<url, parse_error> {
        url u;

        // scheme
        auto scheme_end = raw.find("://");
        if (scheme_end == std::string_view::npos)
            return std::unexpected(parse_error::incomplete);
        u.scheme = std::string{raw.substr(0, scheme_end)};
        raw = raw.substr(scheme_end + 3);

        // host[:port]
        auto path_start = raw.find('/');
        auto authority = (path_start != std::string_view::npos)
            ? raw.substr(0, path_start) : raw;

        auto colon = authority.rfind(':');
        if (colon != std::string_view::npos) {
            u.host = std::string{authority.substr(0, colon)};
            auto port_str = authority.substr(colon + 1);
            std::uint16_t p = 0;
            for (auto c : port_str) {
                if (c < '0' || c > '9')
                    return std::unexpected(parse_error::incomplete);
                p = p * 10 + (c - '0');
            }
            u.port = p;
        } else {
            u.host = std::string{authority};
        }

        if (u.host.empty())
            return std::unexpected(parse_error::incomplete);

        // path?query
        if (path_start != std::string_view::npos) {
            raw = raw.substr(path_start);
            auto q = raw.find('?');
            if (q != std::string_view::npos) {
                u.path = std::string{raw.substr(0, q)};
                u.query = std::string{raw.substr(q + 1)};
            } else {
                u.path = std::string{raw};
            }
        } else {
            u.path = "/";
        }

        if (u.path.empty()) u.path = "/";
        return u;
    }

    auto to_string() const -> std::string {
        auto result = std::format("{}://{}", scheme, host);
        auto default_port = static_cast<std::uint16_t>((scheme == "https") ? 443 : 80);
        if (port != 0 && port != default_port)
            result += std::format(":{}", port);
        result += path;
        if (!query.empty())
            result += std::format("?{}", query);
        return result;
    }
};

// ---- request / response --------------------------------------------------

struct request {
    method verb = method::GET;
    url target;
    headers hdrs;
    std::vector<std::byte> body;
};

struct response {
    status stat;
    headers hdrs;
    std::vector<std::byte> body;

    auto body_string() const -> std::string {
        return std::string{
            reinterpret_cast<char const*>(body.data()), body.size()};
    }
};

// ---- serializer ----------------------------------------------------------

inline auto serialize(request const& req) -> std::vector<std::byte> {
    auto line = std::format("{} {}", to_string(req.verb), req.target.path);
    if (!req.target.query.empty())
        line += std::format("?{}", req.target.query);
    line += " HTTP/1.1\r\n";

    auto out = std::string{std::move(line)};

    // Ensure Host header is present
    bool has_host = false;
    for (auto const& [k, v] : req.hdrs) {
        out += std::format("{}: {}\r\n", k, v);
        if (k == "host") has_host = true;
    }
    if (!has_host) {
        auto host_val = req.target.host;
        auto ep = req.target.effective_port();
        if ((req.target.scheme == "http" && ep != 80) ||
            (req.target.scheme == "https" && ep != 443))
            host_val += std::format(":{}", ep);
        out += std::format("host: {}\r\n", host_val);
    }

    if (!req.body.empty())
        out += std::format("content-length: {}\r\n", req.body.size());

    out += "\r\n";

    auto bytes = std::vector<std::byte>{};
    bytes.reserve(out.size() + req.body.size());
    for (auto c : out)
        bytes.push_back(static_cast<std::byte>(c));
    bytes.insert(bytes.end(), req.body.begin(), req.body.end());
    return bytes;
}

inline auto serialize(response const& res) -> std::vector<std::byte> {
    auto out = std::format("HTTP/1.1 {} {}\r\n",
                           res.stat.code, res.stat.reason());
    for (auto const& [k, v] : res.hdrs)
        out += std::format("{}: {}\r\n", k, v);
    if (!res.body.empty() && !res.hdrs.contains("content-length"))
        out += std::format("content-length: {}\r\n", res.body.size());
    out += "\r\n";

    auto bytes = std::vector<std::byte>{};
    bytes.reserve(out.size() + res.body.size());
    for (auto c : out)
        bytes.push_back(static_cast<std::byte>(c));
    bytes.insert(bytes.end(), res.body.begin(), res.body.end());
    return bytes;
}

// Helper to create body bytes from string
inline auto as_bytes(std::string_view s) -> std::vector<std::byte> {
    auto v = std::vector<std::byte>{};
    v.reserve(s.size());
    for (auto c : s)
        v.push_back(static_cast<std::byte>(c));
    return v;
}

// ---- incremental parser --------------------------------------------------

enum class parse_state { need_more, headers_done, complete };

namespace detail {

// Find \r\n\r\n boundary in accumulated data.
inline auto find_header_end(std::string_view data) -> std::optional<std::size_t> {
    auto pos = data.find("\r\n\r\n");
    if (pos == std::string_view::npos) return std::nullopt;
    return pos + 4; // past the double CRLF
}

// Parse "Key: Value\r\n" lines from header block.
inline auto parse_headers(std::string_view block, headers& hdrs)
    -> std::expected<void, parse_error>
{
    while (!block.empty()) {
        auto eol = block.find("\r\n");
        if (eol == std::string_view::npos) break;
        auto line = block.substr(0, eol);
        block = block.substr(eol + 2);
        if (line.empty()) continue;

        auto colon = line.find(':');
        if (colon == std::string_view::npos)
            return std::unexpected(parse_error::invalid_header);
        auto key = line.substr(0, colon);
        auto val = line.substr(colon + 1);
        // Trim leading whitespace from value
        while (!val.empty() && val.front() == ' ') val = val.substr(1);
        hdrs.append(key, val);
    }
    return {};
}

} // namespace detail

class response_parser {
    std::string buf_;
    std::size_t max_header_;
    std::size_t max_body_;
    bool headers_parsed_ = false;
    response res_;
    std::size_t content_length_ = 0;
    bool chunked_ = false;

public:
    explicit response_parser(std::size_t max_header = 8192,
                             std::size_t max_body = 64 * 1024 * 1024)
        : max_header_{max_header}, max_body_{max_body} {}

    auto feed(std::span<std::byte const> chunk)
        -> std::expected<parse_state, parse_error>
    {
        for (auto b : chunk)
            buf_.push_back(static_cast<char>(b));

        if (!headers_parsed_) {
            if (buf_.size() > max_header_)
                return std::unexpected(parse_error::header_too_large);

            auto end = detail::find_header_end(buf_);
            if (!end) return parse_state::need_more;

            // Parse status line: "HTTP/1.1 200 OK\r\n"
            auto first_line_end = buf_.find("\r\n");
            if (first_line_end == std::string::npos)
                return std::unexpected(parse_error::invalid_status_line);
            auto status_line = std::string_view{buf_}.substr(0, first_line_end);

            // "HTTP/1.x SSS reason"
            if (status_line.size() < 12 || status_line.substr(0, 5) != "HTTP/")
                return std::unexpected(parse_error::invalid_status_line);
            auto sp1 = status_line.find(' ');
            if (sp1 == std::string_view::npos)
                return std::unexpected(parse_error::invalid_status_line);
            auto code_start = sp1 + 1;
            auto sp2 = status_line.find(' ', code_start);
            auto code_str = (sp2 != std::string_view::npos)
                ? status_line.substr(code_start, sp2 - code_start)
                : status_line.substr(code_start);

            std::uint16_t code = 0;
            for (auto c : code_str) {
                if (c < '0' || c > '9')
                    return std::unexpected(parse_error::invalid_status_line);
                code = code * 10 + (c - '0');
            }
            res_.stat = status{code};

            // Parse headers
            auto hdr_block = std::string_view{buf_}.substr(
                first_line_end + 2, *end - first_line_end - 4);
            auto hr = detail::parse_headers(hdr_block, res_.hdrs);
            if (!hr) return std::unexpected(hr.error());

            // Determine body mode
            if (auto te = res_.hdrs.get("transfer-encoding");
                te && te->find("chunked") != std::string_view::npos) {
                chunked_ = true;
            } else if (auto cl = res_.hdrs.get("content-length")) {
                std::size_t len = 0;
                for (auto c : *cl) {
                    if (c < '0' || c > '9')
                        return std::unexpected(parse_error::bad_content_length);
                    len = len * 10 + (c - '0');
                }
                if (len > max_body_)
                    return std::unexpected(parse_error::body_too_large);
                content_length_ = len;
            }

            // Remove consumed header bytes, keep leftover body bytes
            buf_.erase(0, *end);
            headers_parsed_ = true;

            if (content_length_ == 0 && !chunked_)
                return parse_state::complete;
        }

        // Body phase
        if (chunked_) {
            // Simple chunked decoder: look for "0\r\n\r\n" terminator
            if (buf_.find("0\r\n\r\n") != std::string::npos ||
                buf_.find("0\r\n\r\n") != std::string::npos) {
                // Decode all chunks
                auto sv = std::string_view{buf_};
                while (!sv.empty()) {
                    auto crlf = sv.find("\r\n");
                    if (crlf == std::string_view::npos) break;
                    auto size_str = sv.substr(0, crlf);
                    std::size_t chunk_size = 0;
                    for (auto c : size_str) {
                        chunk_size <<= 4;
                        if (c >= '0' && c <= '9') chunk_size += (c - '0');
                        else if (c >= 'a' && c <= 'f') chunk_size += (c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') chunk_size += (c - 'A' + 10);
                        else return std::unexpected(parse_error::bad_chunk_encoding);
                    }
                    if (chunk_size == 0) {
                        return parse_state::complete;
                    }
                    sv = sv.substr(crlf + 2);
                    if (sv.size() < chunk_size + 2) return parse_state::need_more;
                    auto chunk_data = sv.substr(0, chunk_size);
                    for (auto c : chunk_data)
                        res_.body.push_back(static_cast<std::byte>(c));
                    sv = sv.substr(chunk_size + 2); // skip chunk data + \r\n
                    if (res_.body.size() > max_body_)
                        return std::unexpected(parse_error::body_too_large);
                }
                return parse_state::need_more;
            }
            return parse_state::need_more;
        }

        // Content-Length mode
        if (buf_.size() >= content_length_) {
            for (std::size_t i = 0; i < content_length_; ++i)
                res_.body.push_back(static_cast<std::byte>(buf_[i]));
            return parse_state::complete;
        }
        return parse_state::need_more;
    }

    auto finish() && -> response { return std::move(res_); }
};

class request_parser {
    std::string buf_;
    std::size_t max_header_;
    std::size_t max_body_;
    bool headers_parsed_ = false;
    request req_;
    std::size_t content_length_ = 0;

public:
    explicit request_parser(std::size_t max_header = 8192,
                            std::size_t max_body = 16 * 1024 * 1024)
        : max_header_{max_header}, max_body_{max_body} {}

    auto feed(std::span<std::byte const> chunk)
        -> std::expected<parse_state, parse_error>
    {
        for (auto b : chunk)
            buf_.push_back(static_cast<char>(b));

        if (!headers_parsed_) {
            if (buf_.size() > max_header_)
                return std::unexpected(parse_error::header_too_large);

            auto end = detail::find_header_end(buf_);
            if (!end) return parse_state::need_more;

            // Parse request line: "GET /path HTTP/1.1\r\n"
            auto first_line_end = buf_.find("\r\n");
            if (first_line_end == std::string::npos)
                return std::unexpected(parse_error::invalid_request_line);
            auto req_line = std::string_view{buf_}.substr(0, first_line_end);

            auto sp1 = req_line.find(' ');
            if (sp1 == std::string_view::npos)
                return std::unexpected(parse_error::invalid_request_line);
            auto method_str = req_line.substr(0, sp1);
            auto m = method_from_string(method_str);
            if (!m) return std::unexpected(parse_error::invalid_request_line);
            req_.verb = *m;

            auto sp2 = req_line.find(' ', sp1 + 1);
            if (sp2 == std::string_view::npos)
                return std::unexpected(parse_error::invalid_request_line);
            auto path_str = req_line.substr(sp1 + 1, sp2 - sp1 - 1);

            // Split path?query
            auto q = path_str.find('?');
            if (q != std::string_view::npos) {
                req_.target.path = std::string{path_str.substr(0, q)};
                req_.target.query = std::string{path_str.substr(q + 1)};
            } else {
                req_.target.path = std::string{path_str};
            }

            // Parse headers
            auto hdr_block = std::string_view{buf_}.substr(
                first_line_end + 2, *end - first_line_end - 4);
            auto hr = detail::parse_headers(hdr_block, req_.hdrs);
            if (!hr) return std::unexpected(hr.error());

            // Content-Length for body
            if (auto cl = req_.hdrs.get("content-length")) {
                std::size_t len = 0;
                for (auto c : *cl) {
                    if (c < '0' || c > '9')
                        return std::unexpected(parse_error::bad_content_length);
                    len = len * 10 + (c - '0');
                }
                if (len > max_body_)
                    return std::unexpected(parse_error::body_too_large);
                content_length_ = len;
            }

            buf_.erase(0, *end);
            headers_parsed_ = true;

            if (content_length_ == 0)
                return parse_state::complete;
        }

        if (buf_.size() >= content_length_) {
            for (std::size_t i = 0; i < content_length_; ++i)
                req_.body.push_back(static_cast<std::byte>(buf_[i]));
            return parse_state::complete;
        }
        return parse_state::need_more;
    }

    auto finish() && -> request { return std::move(req_); }
};

// ---- engine concepts -----------------------------------------------------

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

// ---- test doubles --------------------------------------------------------

// Null stream that always fails. Useful for compile-time concept
// checking and as a base for test fakes.
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

// ---- async engine concepts -----------------------------------------------

// Async counterparts of the sync engine concepts. Methods return
// task<expected<...>> so they can be co_awaited. The task<T> type
// comes from cppx.async.

template <class T>
concept async_stream_engine = requires(T s,
                                       std::string_view host, std::uint16_t port,
                                       std::span<std::byte const> out,
                                       std::span<std::byte> in) {
    { T::connect(host, port) }
        -> std::same_as<cppx::async::task<std::expected<T, net_error>>>;
    { s.send(out) }
        -> std::same_as<cppx::async::task<std::expected<std::size_t, net_error>>>;
    { s.recv(in)  }
        -> std::same_as<cppx::async::task<std::expected<std::size_t, net_error>>>;
    { s.close() } -> std::same_as<void>;
};

template <class T, class Stream>
concept async_listener_engine = async_stream_engine<Stream> &&
    requires(T l, std::string_view host, std::uint16_t port) {
    { T::bind(host, port) }
        -> std::same_as<cppx::async::task<std::expected<T, net_error>>>;
    { l.accept() }
        -> std::same_as<cppx::async::task<std::expected<Stream, net_error>>>;
    { l.close() } -> std::same_as<void>;
};

template <class T, class RawStream>
concept async_tls_provider = async_stream_engine<RawStream> &&
    requires(T t, RawStream raw, std::string_view hostname) {
    { t.wrap(std::move(raw), hostname) }
        -> std::same_as<cppx::async::task<
               std::expected<typename T::stream, net_error>>>;
};

// ---- async test doubles --------------------------------------------------

struct async_null_stream {
    static auto connect(std::string_view, std::uint16_t)
        -> cppx::async::task<std::expected<async_null_stream, net_error>> {
        co_return std::unexpected(net_error::connect_refused);
    }
    auto send(std::span<std::byte const>) const
        -> cppx::async::task<std::expected<std::size_t, net_error>> {
        co_return std::unexpected(net_error::send_failed);
    }
    auto recv(std::span<std::byte>) const
        -> cppx::async::task<std::expected<std::size_t, net_error>> {
        co_return std::unexpected(net_error::recv_failed);
    }
    void close() const {}
};
static_assert(async_stream_engine<async_null_stream>);

struct async_null_listener {
    static auto bind(std::string_view, std::uint16_t)
        -> cppx::async::task<std::expected<async_null_listener, net_error>> {
        co_return std::unexpected(net_error::bind_failed);
    }
    auto accept() const
        -> cppx::async::task<std::expected<async_null_stream, net_error>> {
        co_return std::unexpected(net_error::accept_failed);
    }
    void close() const {}
};
static_assert(async_listener_engine<async_null_listener, async_null_stream>);

struct async_null_tls {
    using stream = async_null_stream;
    auto wrap(async_null_stream, std::string_view) const
        -> cppx::async::task<std::expected<async_null_stream, net_error>> {
        co_return std::unexpected(net_error::tls_handshake_failed);
    }
};
static_assert(async_tls_provider<async_null_tls, async_null_stream>);

} // namespace cppx::http
