// First-party async HTTP facade over cppx.async.system. This initial
// system adapter intentionally supports plain HTTP only; HTTPS remains
// on the sync cppx.http.system path until an async TLS/backend design
// is added.
//
// On wasm32-wasi there is no async socket backend, so this module
// compiles as an empty stub and callers should not use it there.

export module cppx.http.async.system;

#if !defined(__wasi__)
import cppx.async;
import cppx.async.system;
import cppx.http;
import cppx.http.async;
import cppx.net;
import std;

export namespace cppx::http::async::system {

namespace detail {

struct no_tls {
    using stream = cppx::async::system::async_stream;

    auto wrap(stream, std::string_view) const
        -> cppx::async::task<std::expected<stream, cppx::net::net_error>> {
        co_return std::unexpected(cppx::net::net_error::tls_handshake_failed);
    }
};

inline auto parse_plain_http_url(std::string_view raw)
    -> std::expected<cppx::http::url, cppx::http::http_error>
{
    auto parsed = cppx::http::url::parse(raw);
    if (!parsed)
        return std::unexpected(cppx::http::http_error::url_parse_failed);
    if (parsed->is_tls())
        return std::unexpected(cppx::http::http_error::tls_failed);
    return std::move(*parsed);
}

} // namespace detail

class client {
public:
    auto request(cppx::http::request const& req, int max_redirects = 5,
                 std::size_t max_body = 64 * 1024 * 1024)
        -> cppx::async::task<std::expected<cppx::http::response, cppx::http::http_error>>
    {
        if (req.target.is_tls())
            co_return std::unexpected(cppx::http::http_error::tls_failed);

        auto inner = cppx::http::async::client<
            cppx::async::system::async_stream,
            detail::no_tls>{};
        co_return co_await inner.request(req, max_redirects, max_body);
    }

    auto get(std::string_view url, cppx::http::headers extra = {})
        -> cppx::async::task<std::expected<cppx::http::response, cppx::http::http_error>>
    {
        auto parsed = detail::parse_plain_http_url(url);
        if (!parsed)
            co_return std::unexpected(parsed.error());

        cppx::http::request req;
        req.verb = cppx::http::method::GET;
        req.target = std::move(*parsed);
        req.hdrs = std::move(extra);
        co_return co_await request(req);
    }

    auto head(std::string_view url)
        -> cppx::async::task<std::expected<cppx::http::response, cppx::http::http_error>>
    {
        auto parsed = detail::parse_plain_http_url(url);
        if (!parsed)
            co_return std::unexpected(parsed.error());

        cppx::http::request req;
        req.verb = cppx::http::method::HEAD;
        req.target = std::move(*parsed);
        co_return co_await request(req);
    }

    auto post(std::string_view url,
              std::string_view content_type,
              std::vector<std::byte> body)
        -> cppx::async::task<std::expected<cppx::http::response, cppx::http::http_error>>
    {
        auto parsed = detail::parse_plain_http_url(url);
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
                     std::size_t max_body = cppx::http::default_download_body_limit)
        -> cppx::async::task<std::expected<cppx::http::response, cppx::http::http_error>>
    {
        auto parsed = detail::parse_plain_http_url(url);
        if (!parsed)
            co_return std::unexpected(parsed.error());

        auto inner = cppx::http::async::client<
            cppx::async::system::async_stream,
            detail::no_tls>{};
        co_return co_await inner.download_to(
            parsed->to_string(), path, std::move(extra), max_body);
    }
};

inline auto get(std::string_view url)
    -> cppx::async::task<std::expected<cppx::http::response, cppx::http::http_error>> {
    co_return co_await client{}.get(url);
}

inline auto get(std::string_view url, cppx::http::headers extra)
    -> cppx::async::task<std::expected<cppx::http::response, cppx::http::http_error>> {
    co_return co_await client{}.get(url, std::move(extra));
}

inline auto download(std::string_view url, std::filesystem::path const& path)
    -> cppx::async::task<std::expected<void, cppx::http::http_error>> {
    auto resp = co_await client{}.download_to(url, path);
    if (!resp) co_return std::unexpected(resp.error());
    if (!resp->stat.ok())
        co_return std::unexpected(cppx::http::http_error::response_parse_failed);
    co_return std::expected<void, cppx::http::http_error>{};
}

inline auto download(std::string_view url,
                     std::filesystem::path const& path,
                     cppx::http::headers extra)
    -> cppx::async::task<std::expected<void, cppx::http::http_error>> {
    auto resp = co_await client{}.download_to(url, path, std::move(extra));
    if (!resp) co_return std::unexpected(resp.error());
    if (!resp->stat.ok())
        co_return std::unexpected(cppx::http::http_error::response_parse_failed);
    co_return std::expected<void, cppx::http::http_error>{};
}

} // namespace cppx::http::async::system

#endif // !defined(__wasi__)
