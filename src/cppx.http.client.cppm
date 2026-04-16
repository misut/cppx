// Pure HTTP client. Templated over stream_engine and tls_provider
// concepts — no platform dependency. When given a real engine (from
// cppx.http.system), it does real HTTP. When given a fake engine,
// it's a pure function from request → response, testable without I/O.

export module cppx.http.client;
import std;
import cppx.http;

export namespace cppx::http {

namespace detail {

// Send all bytes from a buffer, retrying on partial sends.
template <stream_engine S>
auto client_send_all(S& s, std::span<std::byte const> data)
    -> std::expected<void, http_error>
{
    while (!data.empty()) {
        auto n = s.send(data);
        if (!n) return std::unexpected(http_error::send_failed);
        data = data.subspan(*n);
    }
    return {};
}

// Perform the HTTP exchange on an already-connected stream:
// serialize request → send → recv loop → parse response.
template <stream_engine S>
auto do_exchange(S& stream, request const& req,
                 std::size_t max_body = 64 * 1024 * 1024)
    -> std::expected<response, http_error>
{
    // Serialize and send
    auto wire = serialize(req);
    auto sr = client_send_all(stream, wire);
    if (!sr) return std::unexpected(sr.error());

    // Receive and parse
    response_parser parser(8192, max_body);
    auto buf = std::array<std::byte, 8192>{};
    for (;;) {
        auto n = stream.recv(buf);
        if (!n) {
            if (n.error() == net_error::connection_closed) {
                // Server closed — finalize what we have. This is
                // normal for HEAD responses and HTTP/1.0 without
                // Content-Length.
                return std::move(parser).finish();
            }
            return std::unexpected(http_error::response_parse_failed);
        }
        auto chunk = std::span<std::byte const>{buf.data(), *n};
        auto state = parser.feed(chunk);
        if (!state)
            return std::unexpected(http_error::response_parse_failed);
        if (*state == parse_state::complete)
            return std::move(parser).finish();
        // HEAD responses: headers are parsed but body is never sent.
        // Once headers are done, return immediately.
        if (*state == parse_state::headers_done &&
            req.verb == method::HEAD)
            return std::move(parser).finish();
    }
}

} // namespace detail

// HTTP client templated over engine concepts. The engine determines
// whether this is a real HTTP client (system engines) or a test
// fixture (fake engines).
//
// Usage with real engines:
//   client<system::stream, system::tls> c;
//   auto resp = c.get("https://api.github.com/...");
//
// Usage in tests:
//   client<fake_stream, fake_tls> c;
//   auto resp = c.get("http://example.com/test");
//
template <stream_engine RawStream, tls_provider<RawStream> Tls>
class client {
    Tls tls_{};

public:
    client() = default;
    explicit client(Tls tls) : tls_{std::move(tls)} {}

    // Send a fully-constructed request and receive the response.
    // Automatically follows 3xx redirects up to max_redirects times.
    auto request(http::request const& req, int max_redirects = 5,
                 std::size_t max_body = 64 * 1024 * 1024)
        -> std::expected<response, http_error>
    {
        auto current = req;
        for (int hops = 0;; ++hops) {
            auto resp = single_request(current, max_body);
            if (!resp) return resp;

            auto code = resp->stat.code;
            if (code < 300 || code >= 400) return resp;
            if (hops >= max_redirects)
                return std::unexpected(http_error::redirect_limit);

            auto location = resp->hdrs.get("location");
            if (!location) return resp;

            auto next = url::parse(*location);
            if (!next) return std::unexpected(http_error::url_parse_failed);

            // 303 See Other: switch to GET regardless of original method
            if (code == 303) current.verb = method::GET;
            current.target = std::move(*next);
            current.hdrs.set("host", current.target.host);
        }
    }

    // Convenience: GET a URL string, returns the full response.
    auto get(std::string_view url_str, headers extra = {})
        -> std::expected<response, http_error>
    {
        auto u = url::parse(url_str);
        if (!u) return std::unexpected(http_error::url_parse_failed);

        http::request req;
        req.verb = method::GET;
        req.target = std::move(*u);
        req.hdrs = std::move(extra);
        return request(req);
    }

    // Convenience: HEAD request (like GET but no body in response).
    auto head(std::string_view url_str)
        -> std::expected<response, http_error>
    {
        auto u = url::parse(url_str);
        if (!u) return std::unexpected(http_error::url_parse_failed);

        http::request req;
        req.verb = method::HEAD;
        req.target = std::move(*u);
        return request(req);
    }

    // Convenience: POST with a body.
    auto post(std::string_view url_str, std::string_view content_type,
              std::vector<std::byte> body)
        -> std::expected<response, http_error>
    {
        auto u = url::parse(url_str);
        if (!u) return std::unexpected(http_error::url_parse_failed);

        http::request req;
        req.verb = method::POST;
        req.target = std::move(*u);
        req.hdrs.set("content-type", content_type);
        req.body = std::move(body);
        return request(req);
    }

    // Download a URL to a local file. Follows redirects. Allows large
    // bodies (up to 512 MiB by default) and writes to disk after the
    // full response is received.
    auto download_to(std::string_view url_str,
                     std::filesystem::path const& path,
                     std::size_t max_body = 512 * 1024 * 1024)
        -> std::expected<response, http_error>
    {
        auto u = url::parse(url_str);
        if (!u) return std::unexpected(http_error::url_parse_failed);

        http::request req;
        req.verb = method::GET;
        req.target = std::move(*u);
        auto resp = request(req, 5, max_body);
        if (!resp) return resp;
        if (!resp->stat.ok()) return resp;

        auto out = std::ofstream{path, std::ios::binary};
        if (!out) return std::unexpected(http_error::send_failed);
        out.write(reinterpret_cast<char const*>(resp->body.data()),
                  static_cast<std::streamsize>(resp->body.size()));
        resp->body.clear();
        return resp;
    }

private:
    auto single_request(http::request const& req,
                        std::size_t max_body = 64 * 1024 * 1024)
        -> std::expected<response, http_error>
    {
        auto const& target = req.target;
        auto port = target.effective_port();

        auto raw = RawStream::connect(target.host, port);
        if (!raw)
            return std::unexpected(http_error::connection_failed);

        if (target.is_tls()) {
            auto tls_stream = tls_.wrap(std::move(*raw), target.host);
            if (!tls_stream)
                return std::unexpected(http_error::tls_failed);
            return detail::do_exchange(*tls_stream, req, max_body);
        }

        return detail::do_exchange(*raw, req, max_body);
    }
};

} // namespace cppx::http
