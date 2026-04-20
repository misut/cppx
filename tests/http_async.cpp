import cppx.async;
import cppx.async.test;
import cppx.http;
import cppx.http.async;
import cppx.bytes;
import cppx.test;
import std;

cppx::test::context tc;

struct fake_async_stream {
    inline static cppx::bytes::byte_buffer next_response{};
    inline static cppx::bytes::byte_buffer last_sent{};

    cppx::bytes::byte_buffer response_data;
    mutable std::size_t recv_pos = 0;
    mutable cppx::bytes::byte_buffer sent;

    static auto connect(std::string_view, std::uint16_t)
        -> cppx::async::task<std::expected<fake_async_stream, cppx::http::net_error>> {
        fake_async_stream s;
        s.response_data = next_response;
        co_return s;
    }

    auto send(std::span<std::byte const> data) const
        -> cppx::async::task<std::expected<std::size_t, cppx::http::net_error>> {
        sent.append(cppx::bytes::bytes_view{data});
        last_sent.append(cppx::bytes::bytes_view{data});
        co_return data.size();
    }

    auto recv(std::span<std::byte> buf) const
        -> cppx::async::task<std::expected<std::size_t, cppx::http::net_error>> {
        if (recv_pos >= response_data.size())
            co_return std::unexpected(cppx::http::net_error::connection_closed);
        auto n = std::min(buf.size(), response_data.size() - recv_pos);
        std::copy_n(response_data.data() + recv_pos, n, buf.data());
        recv_pos += n;
        co_return n;
    }

    void close() const {}
};

struct fake_async_tls {
    using stream = fake_async_stream;
    auto wrap(fake_async_stream raw, std::string_view) const
        -> cppx::async::task<std::expected<fake_async_stream, cppx::http::net_error>> {
        co_return std::move(raw);
    }
};

struct fail_async_stream {
    static auto connect(std::string_view, std::uint16_t)
        -> cppx::async::task<std::expected<fail_async_stream, cppx::http::net_error>> {
        co_return std::unexpected(cppx::http::net_error::connect_refused);
    }
    auto send(std::span<std::byte const>) const
        -> cppx::async::task<std::expected<std::size_t, cppx::http::net_error>> {
        co_return std::unexpected(cppx::http::net_error::send_failed);
    }
    auto recv(std::span<std::byte>) const
        -> cppx::async::task<std::expected<std::size_t, cppx::http::net_error>> {
        co_return std::unexpected(cppx::http::net_error::recv_failed);
    }
    void close() const {}
};

struct fail_async_tls {
    using stream = fail_async_stream;
    auto wrap(fail_async_stream, std::string_view) const
        -> cppx::async::task<std::expected<fail_async_stream, cppx::http::net_error>> {
        co_return std::unexpected(cppx::http::net_error::tls_handshake_failed);
    }
};

struct redirect_async_stream {
    inline static std::vector<cppx::bytes::byte_buffer> responses{};
    inline static std::size_t conn_index = 0;

    cppx::bytes::byte_buffer response_data;
    mutable std::size_t recv_pos = 0;

    static auto connect(std::string_view, std::uint16_t)
        -> cppx::async::task<std::expected<redirect_async_stream, cppx::http::net_error>> {
        redirect_async_stream s;
        if (conn_index < responses.size())
            s.response_data = responses[conn_index++];
        co_return s;
    }

    auto send(std::span<std::byte const>)
        const -> cppx::async::task<std::expected<std::size_t, cppx::http::net_error>> {
        co_return std::size_t{1};
    }

    auto recv(std::span<std::byte> buf) const
        -> cppx::async::task<std::expected<std::size_t, cppx::http::net_error>> {
        if (recv_pos >= response_data.size())
            co_return std::unexpected(cppx::http::net_error::connection_closed);
        auto n = std::min(buf.size(), response_data.size() - recv_pos);
        std::copy_n(response_data.data() + recv_pos, n, buf.data());
        recv_pos += n;
        co_return n;
    }

    void close() const {}
};

struct redirect_async_tls {
    using stream = redirect_async_stream;
    inline static std::size_t wrap_calls = 0;

    auto wrap(redirect_async_stream raw, std::string_view) const
        -> cppx::async::task<std::expected<redirect_async_stream, cppx::http::net_error>> {
        ++wrap_calls;
        co_return std::move(raw);
    }
};

struct tail_limited_async_stream {
    inline static cppx::bytes::byte_buffer next_response{};

    cppx::bytes::byte_buffer response_data;
    mutable std::size_t recv_pos = 0;

    static auto connect(std::string_view, std::uint16_t)
        -> cppx::async::task<std::expected<tail_limited_async_stream, cppx::http::net_error>> {
        tail_limited_async_stream s;
        s.response_data = next_response;
        co_return s;
    }

    auto send(std::span<std::byte const> data) const
        -> cppx::async::task<std::expected<std::size_t, cppx::http::net_error>> {
        co_return data.size();
    }

    auto recv(std::span<std::byte> buf) const
        -> cppx::async::task<std::expected<std::size_t, cppx::http::net_error>> {
        if (recv_pos >= response_data.size())
            co_return std::unexpected(cppx::http::net_error::connection_closed);

        auto remain = response_data.size() - recv_pos;
        if (recv_pos > 0 && buf.size() > remain)
            co_return std::unexpected(cppx::http::net_error::recv_failed);

        auto n = std::min(buf.size(), remain);
        std::copy_n(response_data.data() + recv_pos, n, buf.data());
        recv_pos += n;
        co_return n;
    }

    void close() const {}
};

struct tail_limited_async_tls {
    using stream = tail_limited_async_stream;
    auto wrap(tail_limited_async_stream raw, std::string_view) const
        -> cppx::async::task<std::expected<tail_limited_async_stream, cppx::http::net_error>> {
        co_return std::move(raw);
    }
};

template <class T, class Factory>
auto run_task(Factory&& factory) -> T {
    return cppx::async::test::run_test(
        [&](cppx::async::test::test_executor&) -> cppx::async::task<T> {
            co_return co_await factory();
        });
}

auto make_response_bytes(std::string_view raw) -> cppx::bytes::byte_buffer {
    return cppx::http::as_bytes(raw);
}

void test_get_200() {
    fake_async_stream::next_response = make_response_bytes(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 2\r\n"
        "\r\n"
        "ok");

    auto c = cppx::http::async::client<fake_async_stream, fake_async_tls>{};
    auto resp = run_task<std::expected<cppx::http::response, cppx::http::http_error>>(
        [&] { return c.get("https://example.com/api"); });

    tc.check(resp.has_value(), "async GET succeeds");
    tc.check(resp->stat.code == 200, "async GET status 200");
    tc.check(resp->body_string() == "ok", "async GET body");
}

void test_post() {
    fake_async_stream::next_response = make_response_bytes(
        "HTTP/1.1 201 Created\r\n"
        "Content-Length: 0\r\n"
        "\r\n");

    auto c = cppx::http::async::client<fake_async_stream, fake_async_tls>{};
    auto body = cppx::http::as_bytes(R"({"name":"test"})");
    auto resp = run_task<std::expected<cppx::http::response, cppx::http::http_error>>(
        [&] {
            return c.post("http://example.com/items",
                          "application/json",
                          std::move(body));
        });

    tc.check(resp.has_value(), "async POST succeeds");
    tc.check(resp->stat.code == 201, "async POST status 201");
}

void test_head_no_body() {
    fake_async_stream::next_response = make_response_bytes(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 1000\r\n"
        "\r\n");

    auto c = cppx::http::async::client<fake_async_stream, fake_async_tls>{};
    auto resp = run_task<std::expected<cppx::http::response, cppx::http::http_error>>(
        [&] { return c.head("http://example.com/"); });

    tc.check(resp.has_value(), "async HEAD succeeds");
    tc.check(resp->body.empty(), "async HEAD body empty");
}

void test_connection_refused() {
    auto c = cppx::http::async::client<fail_async_stream, fail_async_tls>{};
    auto resp = run_task<std::expected<cppx::http::response, cppx::http::http_error>>(
        [&] { return c.get("http://example.com/"); });

    tc.check(!resp.has_value(), "async connection refused -> error");
    tc.check(resp.error() == cppx::http::http_error::connection_failed,
             "async connection error kind");
}

void test_redirect_followed() {
    redirect_async_stream::conn_index = 0;
    redirect_async_tls::wrap_calls = 0;
    redirect_async_stream::responses = {
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

    auto c = cppx::http::async::client<redirect_async_stream, redirect_async_tls>{};
    auto resp = run_task<std::expected<cppx::http::response, cppx::http::http_error>>(
        [&] { return c.get("http://example.com/start"); });

    tc.check(resp.has_value(), "async redirect followed");
    tc.check(resp->stat.code == 200, "async redirect final status");
    tc.check(resp->body_string() == "done", "async redirect final body");
    tc.check(redirect_async_tls::wrap_calls == 0,
             "async redirect without TLS keeps raw transport");
}

void test_redirect_to_https() {
    redirect_async_stream::conn_index = 0;
    redirect_async_tls::wrap_calls = 0;
    redirect_async_stream::responses = {
        make_response_bytes(
            "HTTP/1.1 302 Found\r\n"
            "Location: https://example.com/final\r\n"
            "Content-Length: 0\r\n"
            "\r\n"),
        make_response_bytes(
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 6\r\n"
            "\r\n"
            "secure"),
    };

    auto c = cppx::http::async::client<redirect_async_stream, redirect_async_tls>{};
    auto resp = run_task<std::expected<cppx::http::response, cppx::http::http_error>>(
        [&] { return c.get("http://example.com/start"); });

    tc.check(resp.has_value(), "async redirect to https followed");
    tc.check(resp->stat.code == 200, "async redirect to https final status");
    tc.check(resp->body_string() == "secure",
             "async redirect to https final body");
    tc.check(redirect_async_tls::wrap_calls == 1,
             "async redirect to https upgrades through TLS provider");
}

void test_chunked_response() {
    fake_async_stream::next_response = make_response_bytes(
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "6\r\n world\r\n"
        "0\r\n\r\n");

    auto c = cppx::http::async::client<fake_async_stream, fake_async_tls>{};
    auto resp = run_task<std::expected<cppx::http::response, cppx::http::http_error>>(
        [&] { return c.get("http://example.com/chunked"); });

    tc.check(resp.has_value(), "async chunked response parsed");
    tc.check(resp->body_string() == "hello world", "async chunked body");
}

void test_download_to() {
    fake_async_stream::next_response = make_response_bytes(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 11\r\n"
        "\r\n"
        "hello world");

    auto c = cppx::http::async::client<fake_async_stream, fake_async_tls>{};
    auto path = std::filesystem::temp_directory_path() / "cppx_async_download.bin";
    auto resp = run_task<std::expected<cppx::http::response, cppx::http::http_error>>(
        [&] { return c.download_to("http://example.com/file", path); });

    tc.check(resp.has_value(), "async download_to succeeds");
    tc.check(resp->stat.code == 200, "async download status 200");
    tc.check(resp->body.empty(), "async download clears body after write");

#if !defined(_WIN32)
    auto in = std::ifstream{path, std::ios::binary};
    auto content = std::string{std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>()};
    tc.check(content == "hello world", "async download file contents");
#endif

    std::filesystem::remove(path);
}

void test_download_tail_limited_stream() {
    tail_limited_async_stream::next_response = make_response_bytes(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 11\r\n"
        "\r\n"
        "hello world");

    auto c = cppx::http::async::client<
        tail_limited_async_stream,
        tail_limited_async_tls>{};
    auto path = std::filesystem::temp_directory_path() / "cppx_async_tail.bin";
    auto resp = run_task<std::expected<cppx::http::response, cppx::http::http_error>>(
        [&] { return c.download_to("http://example.com/file", path); });

    tc.check(resp.has_value(), "async download respects remaining content length");
    std::filesystem::remove(path);
}

void test_get_tail_limited_stream() {
    auto body = std::string(9000, 'x');
    tail_limited_async_stream::next_response = make_response_bytes(std::format(
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: {}\r\n"
        "\r\n"
        "{}",
        body.size(), body));

    auto c = cppx::http::async::client<
        tail_limited_async_stream,
        tail_limited_async_tls>{};
    auto resp = run_task<std::expected<cppx::http::response, cppx::http::http_error>>(
        [&] { return c.get("http://example.com/file"); });

    tc.check(resp.has_value(), "async GET respects remaining content length");
    tc.check(resp && resp->body_string() == body, "async GET body contents");
}

int main() {
    test_get_200();
    test_post();
    test_head_no_body();
    test_connection_refused();
    test_redirect_followed();
    test_redirect_to_https();
    test_chunked_response();
    test_download_to();
    test_get_tail_limited_stream();
    test_download_tail_limited_stream();
    return tc.summary("cppx.http.async");
}
