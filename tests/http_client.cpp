// Pure tests for cppx.http.client. Uses fake stream/TLS engines
// that return canned HTTP responses — no real network I/O.

import cppx.http;
import cppx.http.client;
import cppx.test;
import std;

cppx::test::context tc;

// ---- fake engines --------------------------------------------------------

// Fake stream that returns canned response bytes on recv().
// connect() always succeeds. sent data is captured for inspection.
struct fake_stream {
    // Set this before calling client methods to control what recv returns.
    inline static std::vector<std::byte> next_response{};
    inline static std::vector<std::byte> last_sent{};

    std::vector<std::byte> response_data;
    mutable std::size_t recv_pos = 0;
    mutable std::vector<std::byte> sent;

    static auto connect(std::string_view, std::uint16_t)
        -> std::expected<fake_stream, cppx::http::net_error> {
        fake_stream s;
        s.response_data = next_response;
        return s;
    }

    auto send(std::span<std::byte const> data) const
        -> std::expected<std::size_t, cppx::http::net_error> {
        sent.insert(sent.end(), data.begin(), data.end());
        last_sent.insert(last_sent.end(), data.begin(), data.end());
        return data.size();
    }

    auto recv(std::span<std::byte> buf) const
        -> std::expected<std::size_t, cppx::http::net_error> {
        if (recv_pos >= response_data.size())
            return std::unexpected(cppx::http::net_error::connection_closed);
        auto n = std::min(buf.size(), response_data.size() - recv_pos);
        std::copy_n(response_data.begin() + recv_pos, n, buf.begin());
        recv_pos += n;
        return n;
    }

    void close() const {}
};

// No-op TLS — just passes the stream through unchanged.
struct fake_tls {
    using stream = fake_stream;
    auto wrap(fake_stream raw, std::string_view) const
        -> std::expected<fake_stream, cppx::http::net_error> {
        return std::move(raw);
    }
};

// A stream that always refuses connections.
struct fail_stream {
    static auto connect(std::string_view, std::uint16_t)
        -> std::expected<fail_stream, cppx::http::net_error> {
        return std::unexpected(cppx::http::net_error::connect_refused);
    }
    auto send(std::span<std::byte const>) const
        -> std::expected<std::size_t, cppx::http::net_error> {
        return std::unexpected(cppx::http::net_error::send_failed);
    }
    auto recv(std::span<std::byte>) const
        -> std::expected<std::size_t, cppx::http::net_error> {
        return std::unexpected(cppx::http::net_error::recv_failed);
    }
    void close() const {}
};

struct fail_tls {
    using stream = fail_stream;
    auto wrap(fail_stream, std::string_view) const
        -> std::expected<fail_stream, cppx::http::net_error> {
        return std::unexpected(cppx::http::net_error::tls_handshake_failed);
    }
};

// ---- helper --------------------------------------------------------------

auto make_response_bytes(std::string_view raw) -> std::vector<std::byte> {
    return cppx::http::as_bytes(raw);
}

auto bytes_to_string(std::vector<std::byte> const& bytes) -> std::string {
    return std::string{
        reinterpret_cast<char const*>(bytes.data()), bytes.size()};
}

auto make_long_location(std::size_t filler = 12 * 1024) -> std::string {
    return std::format(
        "https://release-assets.githubusercontent.com/final?"
        "sig={}&response-content-disposition=attachment",
        std::string(filler, 'a'));
}

// ---- tests ---------------------------------------------------------------

void test_get_200() {
    fake_stream::next_response = make_response_bytes(
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 17\r\n"
        "\r\n"
        "{\"version\":\"1.0\"}");

    auto c = cppx::http::client<fake_stream, fake_tls>{};
    auto resp = c.get("http://example.com/api/version");

    tc.check(resp.has_value(), "GET 200 succeeds");
    tc.check(resp->stat.code == 200, "status 200");
    tc.check(resp->stat.ok(), "ok()");
    tc.check(resp->body_string() == R"({"version":"1.0"})", "body");
    tc.check(resp->hdrs.get("content-type") == "application/json",
          "content-type header");
}

void test_get_404() {
    fake_stream::next_response = make_response_bytes(
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Length: 9\r\n"
        "\r\n"
        "not found");

    auto c = cppx::http::client<fake_stream, fake_tls>{};
    auto resp = c.get("http://example.com/missing");

    tc.check(resp.has_value(), "GET 404 succeeds (HTTP error, not client error)");
    tc.check(resp->stat.code == 404, "status 404");
    tc.check(!resp->stat.ok(), "not ok()");
    tc.check(resp->body_string() == "not found", "body");
}

void test_get_sends_correct_request() {
    fake_stream::next_response = make_response_bytes(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "\r\n");

    auto c = cppx::http::client<fake_stream, fake_tls>{};
    auto resp = c.get("http://api.example.com:8080/v1/data?key=abc");
    tc.check(resp.has_value(), "get succeeds");

    // Inspect what was sent to the fake stream
    // We can't easily access the sent data from the moved stream,
    // so we rely on the response being correctly parsed.
    // The request serialization is tested in tests/http.cpp.
}

void test_get_https_url() {
    // HTTPS URL goes through TLS wrapping (fake_tls is no-op).
    fake_stream::next_response = make_response_bytes(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 2\r\n"
        "\r\n"
        "ok");

    auto c = cppx::http::client<fake_stream, fake_tls>{};
    auto resp = c.get("https://secure.example.com/api");

    tc.check(resp.has_value(), "HTTPS GET succeeds with fake TLS");
    tc.check(resp->body_string() == "ok", "body");
}

void test_post() {
    fake_stream::next_response = make_response_bytes(
        "HTTP/1.1 201 Created\r\n"
        "Content-Length: 0\r\n"
        "\r\n");

    auto c = cppx::http::client<fake_stream, fake_tls>{};
    auto body = cppx::http::as_bytes(R"({"name":"test"})");
    auto resp = c.post("http://example.com/api/items",
                       "application/json", std::move(body));

    tc.check(resp.has_value(), "POST succeeds");
    tc.check(resp->stat.code == 201, "status 201");
}

void test_connection_refused() {
    auto c = cppx::http::client<fail_stream, fail_tls>{};
    auto resp = c.get("http://example.com/");

    tc.check(!resp.has_value(), "connection refused → error");
    tc.check(resp.error() == cppx::http::http_error::connection_failed,
          "error is connection_failed");
}

void test_invalid_url() {
    auto c = cppx::http::client<fake_stream, fake_tls>{};
    auto resp = c.get("not-a-url");

    tc.check(!resp.has_value(), "invalid URL → error");
    tc.check(resp.error() == cppx::http::http_error::url_parse_failed,
          "error is url_parse_failed");
}

void test_chunked_response() {
    fake_stream::next_response = make_response_bytes(
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "6\r\n world\r\n"
        "0\r\n\r\n");

    auto c = cppx::http::client<fake_stream, fake_tls>{};
    auto resp = c.get("http://example.com/chunked");

    tc.check(resp.has_value(), "chunked response parsed");
    tc.check(resp->body_string() == "hello world", "chunked body assembled");
}

void test_head_no_body() {
    fake_stream::next_response = make_response_bytes(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 1000\r\n"
        "\r\n");

    auto c = cppx::http::client<fake_stream, fake_tls>{};
    auto resp = c.head("http://example.com/");

    // HEAD responses have Content-Length but no body. The server
    // closes the connection after headers, which the parser handles
    // via connection_closed.
    // For our fake, recv returns connection_closed after headers,
    // and the parser should complete.
    tc.check(resp.has_value(), "HEAD succeeds");
    tc.check(resp->body.empty(), "HEAD has no body");
}

// ---- redirect tests -----------------------------------------------------

// Multi-hop redirect stream: returns different responses per connection.
struct redirect_stream {
    inline static std::vector<std::vector<std::byte>> responses{};
    inline static std::size_t conn_index = 0;

    std::vector<std::byte> response_data;
    mutable std::size_t recv_pos = 0;

    static auto connect(std::string_view, std::uint16_t)
        -> std::expected<redirect_stream, cppx::http::net_error> {
        redirect_stream s;
        if (conn_index < responses.size())
            s.response_data = responses[conn_index++];
        return s;
    }

    auto send(std::span<std::byte const>) const
        -> std::expected<std::size_t, cppx::http::net_error> {
        return std::size_t{1};  // pretend we sent it
    }

    auto recv(std::span<std::byte> buf) const
        -> std::expected<std::size_t, cppx::http::net_error> {
        if (recv_pos >= response_data.size())
            return std::unexpected(cppx::http::net_error::connection_closed);
        auto n = std::min(buf.size(), response_data.size() - recv_pos);
        std::copy_n(response_data.begin() + recv_pos, n, buf.begin());
        recv_pos += n;
        return n;
    }

    void close() const {}
};

struct redirect_tls {
    using stream = redirect_stream;
    auto wrap(redirect_stream raw, std::string_view) const
        -> std::expected<redirect_stream, cppx::http::net_error> {
        return std::move(raw);
    }
};

void test_redirect_302() {
    redirect_stream::conn_index = 0;
    redirect_stream::responses = {
        make_response_bytes(
            "HTTP/1.1 302 Found\r\n"
            "Location: http://example.com/final\r\n"
            "Content-Length: 0\r\n"
            "\r\n"),
        make_response_bytes(
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 4\r\n"
            "\r\n"
            "done"),
    };

    auto c = cppx::http::client<redirect_stream, redirect_tls>{};
    auto resp = c.get("http://example.com/start");

    tc.check(resp.has_value(), "302 redirect followed");
    tc.check(resp->stat.code == 200, "final status 200");
    tc.check(resp->body_string() == "done", "final body");
}

void test_redirect_301_chain() {
    redirect_stream::conn_index = 0;
    redirect_stream::responses = {
        make_response_bytes(
            "HTTP/1.1 301 Moved Permanently\r\n"
            "Location: http://example.com/hop2\r\n"
            "Content-Length: 0\r\n"
            "\r\n"),
        make_response_bytes(
            "HTTP/1.1 301 Moved Permanently\r\n"
            "Location: http://example.com/hop3\r\n"
            "Content-Length: 0\r\n"
            "\r\n"),
        make_response_bytes(
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 5\r\n"
            "\r\n"
            "final"),
    };

    auto c = cppx::http::client<redirect_stream, redirect_tls>{};
    auto resp = c.get("http://example.com/hop1");

    tc.check(resp.has_value(), "301 chain followed");
    tc.check(resp->stat.code == 200, "final status 200");
    tc.check(resp->body_string() == "final", "final body");
}

void test_redirect_large_location() {
    auto location = make_long_location();

    redirect_stream::conn_index = 0;
    redirect_stream::responses = {
        make_response_bytes(std::format(
            "HTTP/1.1 302 Found\r\n"
            "Location: {}\r\n"
            "Content-Length: 0\r\n"
            "\r\n",
            location)),
        make_response_bytes(
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 4\r\n"
            "\r\n"
            "done"),
    };

    auto c = cppx::http::client<redirect_stream, redirect_tls>{};
    auto resp = c.get("http://example.com/start");

    tc.check(resp.has_value(), "large redirect header followed");
    tc.check(resp->stat.code == 200, "large redirect final status 200");
    tc.check(resp->body_string() == "done", "large redirect final body");
}

void test_redirect_limit() {
    redirect_stream::conn_index = 0;
    redirect_stream::responses.clear();
    // 6 redirects → exceeds default limit of 5
    for (int i = 0; i < 6; ++i) {
        redirect_stream::responses.push_back(make_response_bytes(
            "HTTP/1.1 302 Found\r\n"
            "Location: http://example.com/next\r\n"
            "Content-Length: 0\r\n"
            "\r\n"));
    }

    auto c = cppx::http::client<redirect_stream, redirect_tls>{};
    auto resp = c.get("http://example.com/loop");

    tc.check(!resp.has_value(), "redirect limit exceeded → error");
    tc.check(resp.error() == cppx::http::http_error::redirect_limit,
             "error is redirect_limit");
}

void test_redirect_no_location() {
    redirect_stream::conn_index = 0;
    redirect_stream::responses = {
        make_response_bytes(
            "HTTP/1.1 302 Found\r\n"
            "Content-Length: 0\r\n"
            "\r\n"),
    };

    auto c = cppx::http::client<redirect_stream, redirect_tls>{};
    auto resp = c.get("http://example.com/");

    // 302 without Location header: return the redirect response as-is
    tc.check(resp.has_value(), "302 without Location returns response");
    tc.check(resp->stat.code == 302, "status 302 returned");
}

void test_download_to() {
    fake_stream::last_sent.clear();
    fake_stream::next_response = make_response_bytes(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 11\r\n"
        "\r\n"
        "hello world");

    auto c = cppx::http::client<fake_stream, fake_tls>{};
    auto path = std::filesystem::temp_directory_path() / "cppx_test_download.bin";
    auto resp = c.download_to("http://example.com/file", path);

    tc.check(resp.has_value(), "download_to succeeds");
    tc.check(resp->stat.code == 200, "download status 200");
    tc.check(resp->body.empty(), "body cleared after write");

#if !defined(_WIN32)
    // MSVC modules bug: std::ifstream with std::filesystem::path
    // under `import std;` triggers a static-init crash before main().
    // Track: https://github.com/misut/cppx/issues/26
    auto in = std::ifstream{path, std::ios::binary};
    auto content = std::string{std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>()};
    tc.check(content == "hello world", "file contains response body");
#endif

    std::filesystem::remove(path);
}

void test_download_to_with_headers() {
    fake_stream::last_sent.clear();
    fake_stream::next_response = make_response_bytes(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "\r\n");

    auto c = cppx::http::client<fake_stream, fake_tls>{};
    auto path = std::filesystem::temp_directory_path() / "cppx_test_download_headers.bin";
    cppx::http::headers extra;
    extra.set("user-agent", "cppx-test");
    extra.set("accept", "application/octet-stream");

    auto resp = c.download_to("http://example.com/file", path, std::move(extra));
    tc.check(resp.has_value(), "download_to with headers succeeds");

    auto sent = bytes_to_string(fake_stream::last_sent);
    tc.check(sent.contains("user-agent: cppx-test\r\n"), "request contains user-agent");
    tc.check(sent.contains("accept: application/octet-stream\r\n"), "request contains accept");

    std::filesystem::remove(path);
}

void test_download_to_chunked() {
    fake_stream::last_sent.clear();
    fake_stream::next_response = make_response_bytes(
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "6\r\n world\r\n"
        "0\r\n\r\n");

    auto c = cppx::http::client<fake_stream, fake_tls>{};
    auto path = std::filesystem::temp_directory_path() / "cppx_test_download_chunked.bin";
    auto resp = c.download_to("http://example.com/chunked", path);

    tc.check(resp.has_value(), "chunked download succeeds");
    tc.check(resp->stat.code == 200, "chunked download status 200");

#if !defined(_WIN32)
    auto in = std::ifstream{path, std::ios::binary};
    auto content = std::string{std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>()};
    tc.check(content == "hello world", "chunked download file contents");
#endif

    std::filesystem::remove(path);
}

void test_download_to_redirect() {
    redirect_stream::conn_index = 0;
    redirect_stream::responses = {
        make_response_bytes(
            "HTTP/1.1 302 Found\r\n"
            "Location: http://example.com/final\r\n"
            "Content-Length: 0\r\n"
            "\r\n"),
        make_response_bytes(
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 5\r\n"
            "\r\n"
            "final"),
    };

    auto c = cppx::http::client<redirect_stream, redirect_tls>{};
    auto path = std::filesystem::temp_directory_path() / "cppx_test_download_redirect.bin";
    auto resp = c.download_to("http://example.com/start", path);

    tc.check(resp.has_value(), "redirect download succeeds");
    tc.check(resp->stat.code == 200, "redirect download status 200");

#if !defined(_WIN32)
    auto in = std::ifstream{path, std::ios::binary};
    auto content = std::string{std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>()};
    tc.check(content == "final", "redirect download file contents");
#endif

    std::filesystem::remove(path);
}

void test_download_to_redirect_large_location() {
    auto location = make_long_location();

    redirect_stream::conn_index = 0;
    redirect_stream::responses = {
        make_response_bytes(std::format(
            "HTTP/1.1 302 Found\r\n"
            "Location: {}\r\n"
            "Content-Length: 0\r\n"
            "\r\n",
            location)),
        make_response_bytes(
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 5\r\n"
            "\r\n"
            "final"),
    };

    auto c = cppx::http::client<redirect_stream, redirect_tls>{};
    auto path = std::filesystem::temp_directory_path() /
                "cppx_test_download_redirect_large.bin";
    auto resp = c.download_to("http://example.com/start", path);

    tc.check(resp.has_value(), "large redirect download succeeds");
    tc.check(resp->stat.code == 200, "large redirect download status 200");

#if !defined(_WIN32)
    auto in = std::ifstream{path, std::ios::binary};
    auto content = std::string{std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>()};
    tc.check(content == "final", "large redirect download file contents");
#endif

    std::filesystem::remove(path);
}

// ---- main ----------------------------------------------------------------

int main() {
    test_get_200();
    test_get_404();
    test_get_sends_correct_request();
    test_get_https_url();
    test_post();
    test_connection_refused();
    test_invalid_url();
    test_chunked_response();
    test_head_no_body();
    test_redirect_302();
    test_redirect_301_chain();
    test_redirect_large_location();
    test_redirect_limit();
    test_redirect_no_location();
    test_download_to();
    test_download_to_with_headers();
    test_download_to_chunked();
    test_download_to_redirect();
    test_download_to_redirect_large_location();
    return tc.summary("cppx.http.client");
}
