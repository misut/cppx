// Deterministic test doubles for cppx.http.system. These fakes let
// tests script the first-party sync HTTP boundary without touching real
// sockets or external networks.

export module cppx.http.system.test;

import cppx.http;
import std;

export namespace cppx::http::system::test {

using response_result =
    std::expected<cppx::http::response, cppx::http::http_error>;

struct test_client {
    response_result next_get =
        std::unexpected{cppx::http::http_error::connection_failed};
    response_result next_download =
        std::unexpected{cppx::http::http_error::connection_failed};

    std::optional<std::string> last_url;
    cppx::http::headers last_headers;
    std::optional<std::filesystem::path> last_download_path;
    std::optional<std::size_t> last_max_body;

    auto get(std::string_view url, cppx::http::headers extra = {})
        -> response_result {
        last_url = std::string{url};
        last_headers = std::move(extra);
        return next_get;
    }

    auto download_to(
        std::string_view url,
        std::filesystem::path const& path,
        cppx::http::headers extra = {},
        std::size_t max_body = cppx::http::default_download_body_limit)
        -> response_result {
        last_url = std::string{url};
        last_download_path = path;
        last_headers = std::move(extra);
        last_max_body = max_body;
        return next_download;
    }
};

} // namespace cppx::http::system::test
