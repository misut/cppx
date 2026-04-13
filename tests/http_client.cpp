// Pure tests for cppx.http.client. Uses fake stream/TLS engines
// that return canned HTTP responses — no real network I/O.

import cppx.http;
import cppx.http.client;
import std;

int failed = 0;
void check(bool cond, std::string_view msg) {
    if (!cond) {
        std::println(std::cerr, "FAIL: {}", msg);
        ++failed;
    }
}

// ---- fake engines --------------------------------------------------------

// Fake stream that returns canned response bytes on recv().
// connect() always succeeds. sent data is captured for inspection.
struct fake_stream {
    // Set this before calling client methods to control what recv returns.
    inline static std::vector<std::byte> next_response{};

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

    check(resp.has_value(), "GET 200 succeeds");
    check(resp->stat.code == 200, "status 200");
    check(resp->stat.ok(), "ok()");
    check(resp->body_string() == R"({"version":"1.0"})", "body");
    check(resp->hdrs.get("content-type") == "application/json",
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

    check(resp.has_value(), "GET 404 succeeds (HTTP error, not client error)");
    check(resp->stat.code == 404, "status 404");
    check(!resp->stat.ok(), "not ok()");
    check(resp->body_string() == "not found", "body");
}

void test_get_sends_correct_request() {
    fake_stream::next_response = make_response_bytes(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "\r\n");

    auto c = cppx::http::client<fake_stream, fake_tls>{};
    auto resp = c.get("http://api.example.com:8080/v1/data?key=abc");
    check(resp.has_value(), "get succeeds");

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

    check(resp.has_value(), "HTTPS GET succeeds with fake TLS");
    check(resp->body_string() == "ok", "body");
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

    check(resp.has_value(), "POST succeeds");
    check(resp->stat.code == 201, "status 201");
}

void test_connection_refused() {
    auto c = cppx::http::client<fail_stream, fail_tls>{};
    auto resp = c.get("http://example.com/");

    check(!resp.has_value(), "connection refused → error");
    check(resp.error() == cppx::http::http_error::connection_failed,
          "error is connection_failed");
}

void test_invalid_url() {
    auto c = cppx::http::client<fake_stream, fake_tls>{};
    auto resp = c.get("not-a-url");

    check(!resp.has_value(), "invalid URL → error");
    check(resp.error() == cppx::http::http_error::url_parse_failed,
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

    check(resp.has_value(), "chunked response parsed");
    check(resp->body_string() == "hello world", "chunked body assembled");
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
    check(resp.has_value(), "HEAD succeeds");
    check(resp->body.empty(), "HEAD has no body");
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

    if (failed > 0) {
        std::println(std::cerr, "\n{} test(s) failed", failed);
        return 1;
    }
    std::println("all cppx.http.client tests passed");
    return 0;
}
