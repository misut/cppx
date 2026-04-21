// Unified system-backed resource reads. Callers pass a locator once and
// this layer hides whether the bytes came from the filesystem or HTTP.

export module cppx.resource.system;
import cppx.bytes;
import cppx.fs;
import cppx.fs.system;
import cppx.http;
import cppx.http.system;
import cppx.resource;
import std;

export namespace cppx::resource::system {

enum class resource_read_error_code {
    unsupported,
    not_found,
    read_failed,
    transport_failed,
};

inline constexpr auto to_string(resource_read_error_code code)
    -> std::string_view {
    switch (code) {
    case resource_read_error_code::unsupported:
        return "unsupported";
    case resource_read_error_code::not_found:
        return "not_found";
    case resource_read_error_code::read_failed:
        return "read_failed";
    case resource_read_error_code::transport_failed:
        return "transport_failed";
    }
    return "unsupported";
}

struct resource_read_error {
    resource_read_error_code code = resource_read_error_code::unsupported;
    std::string message;
    cppx::resource::resource_kind kind = cppx::resource::resource_kind::other_url;
    std::optional<cppx::fs::fs_error> fs_error;
    std::optional<cppx::http::http_error> http_error;
    std::optional<std::uint16_t> http_status_code;
};

} // namespace cppx::resource::system

namespace cppx::resource::system::detail {

inline auto make_error(
        cppx::resource::system::resource_read_error_code code,
        cppx::resource::resource_kind kind,
        std::string message,
        std::optional<cppx::fs::fs_error> fs_error = std::nullopt,
        std::optional<cppx::http::http_error> http_error = std::nullopt,
        std::optional<std::uint16_t> http_status_code = std::nullopt)
    -> std::unexpected<cppx::resource::system::resource_read_error> {
    return std::unexpected(cppx::resource::system::resource_read_error{
        .code = code,
        .message = std::move(message),
        .kind = kind,
        .fs_error = fs_error,
        .http_error = http_error,
        .http_status_code = http_status_code,
    });
}

inline auto unsupported_locator(std::string_view locator,
                                cppx::resource::resource_kind kind,
                                std::string_view reason)
    -> std::unexpected<cppx::resource::system::resource_read_error> {
    return make_error(
        cppx::resource::system::resource_read_error_code::unsupported,
        kind,
        std::format("unsupported locator '{}': {}", locator, reason));
}

inline auto map_fs_error(std::string_view locator,
                         cppx::resource::resource_kind kind,
                         std::filesystem::path const& path,
                         cppx::fs::fs_error error)
    -> std::unexpected<cppx::resource::system::resource_read_error> {
    auto const code = error == cppx::fs::fs_error::not_found
        ? cppx::resource::system::resource_read_error_code::not_found
        : cppx::resource::system::resource_read_error_code::read_failed;

    return make_error(
        code,
        kind,
        std::format(
            "could not read resource '{}' from path '{}': {}",
            locator,
            path.string(),
            cppx::fs::to_string(error)),
        error);
}

inline auto map_http_status(std::string_view locator,
                            cppx::resource::resource_kind kind,
                            std::uint16_t status_code)
    -> std::unexpected<cppx::resource::system::resource_read_error> {
    auto const code = status_code == 404
        ? cppx::resource::system::resource_read_error_code::not_found
        : cppx::resource::system::resource_read_error_code::read_failed;

    return make_error(
        code,
        kind,
        std::format("request for resource '{}' returned HTTP {}", locator, status_code),
        std::nullopt,
        std::nullopt,
        status_code);
}

inline auto map_http_error(std::string_view locator,
                           cppx::resource::resource_kind kind,
                           cppx::http::http_error error)
    -> std::unexpected<cppx::resource::system::resource_read_error> {
    return make_error(
        cppx::resource::system::resource_read_error_code::transport_failed,
        kind,
        std::format(
            "request for resource '{}' failed: {}",
            locator,
            cppx::http::to_string(error)),
        std::nullopt,
        error);
}

template <class HttpClient>
inline auto read_remote_bytes(std::string_view locator,
                              cppx::resource::resource_kind kind,
                              cppx::http::headers headers,
                              HttpClient& http_client)
    -> std::expected<cppx::bytes::byte_buffer,
                     cppx::resource::system::resource_read_error> {
    auto response = http_client.get(locator, std::move(headers));
    if (!response)
        return map_http_error(locator, kind, response.error());
    if (!response->stat.ok())
        return map_http_status(locator, kind, response->stat.code);
    return std::move(response->body);
}

} // namespace cppx::resource::system::detail

export namespace cppx::resource::system {

template <class HttpClient>
    requires requires(HttpClient& http_client,
                      std::string_view url,
                      cppx::http::headers headers) {
        { http_client.get(url, std::move(headers)) }
            -> std::same_as<
                std::expected<cppx::http::response, cppx::http::http_error>>;
    }
inline auto read_bytes(std::filesystem::path const& base,
                       std::string_view locator,
                       HttpClient& http_client,
                       cppx::http::headers headers = {})
    -> std::expected<cppx::bytes::byte_buffer, resource_read_error> {
    auto const kind = cppx::resource::classify(locator);

    switch (kind) {
    case cppx::resource::resource_kind::filesystem_path: {
        auto const path = cppx::resource::resolve_path(base, locator);
        auto bytes = cppx::fs::system::read_bytes(path);
        if (!bytes)
            return detail::map_fs_error(locator, kind, path, bytes.error());
        return std::move(*bytes);
    }
    case cppx::resource::resource_kind::file_url: {
        auto path = cppx::resource::resolve_file_url(locator);
        if (!path) {
            return detail::unsupported_locator(
                locator,
                kind,
                "local file URI only");
        }

        auto bytes = cppx::fs::system::read_bytes(*path);
        if (!bytes)
            return detail::map_fs_error(locator, kind, *path, bytes.error());
        return std::move(*bytes);
    }
    case cppx::resource::resource_kind::http_url:
    case cppx::resource::resource_kind::https_url:
#if defined(__wasi__) || defined(__ANDROID__)
        return detail::unsupported_locator(
            locator,
            kind,
            "remote resource reads are unavailable on this platform "
            "(wasi / android have no built-in HTTPS backend)");
#else
        return detail::read_remote_bytes(
            locator,
            kind,
            std::move(headers),
            http_client);
#endif
    case cppx::resource::resource_kind::other_url:
        return detail::unsupported_locator(locator, kind, "unsupported locator scheme");
    }

    return detail::unsupported_locator(locator, kind, "unsupported locator scheme");
}

#if !defined(__wasi__) && !defined(__ANDROID__)
// Convenience overload that constructs a default `cppx::http::system::client`.
// Only available on platforms that ship a TLS backend (macOS / Linux / Windows);
// wasi and android stub out http.system, so callers there must pass an explicit
// HttpClient that satisfies the concept on the templated overload above.
inline auto read_bytes(std::filesystem::path const& base,
                       std::string_view locator,
                       cppx::http::headers headers = {})
    -> std::expected<cppx::bytes::byte_buffer, resource_read_error> {
    auto http_client = cppx::http::system::client{};
    return read_bytes(base, locator, http_client, std::move(headers));
}
#endif

} // namespace cppx::resource::system
