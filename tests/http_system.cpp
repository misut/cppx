// Smoke test for cppx.http.system socket engines. Starts a listener
// on an ephemeral port, connects with a stream, sends/receives data,
// and verifies the round-trip. This is the only HTTP test that touches
// real sockets — the pure parser/types tests are in tests/http.cpp.

import cppx.http;
import cppx.http.system;
import cppx.bytes;
import cppx.test;
import std;

cppx::test::context tc;

auto http_test_user_agent() -> cppx::http::headers {
    auto hdrs = cppx::http::headers{};
    hdrs.set("user-agent", "cppx/test-http_system");
    return hdrs;
}

auto parse_content_length(std::string_view value) -> std::optional<std::size_t> {
    if (value.empty())
        return std::nullopt;
    std::size_t length = 0;
    for (auto ch : value) {
        if (ch < '0' || ch > '9')
            return std::nullopt;
        length = length * 10 + static_cast<std::size_t>(ch - '0');
    }
    return length;
}

void test_tcp_roundtrip() {
    // Bind listener on ephemeral port
    auto listener = cppx::http::system::listener::bind("127.0.0.1", 0);
    tc.check(listener.has_value(), "listener bind succeeded");
    if (!listener) return;

    auto port = listener->local_port();
    tc.check(port > 0, "got ephemeral port");

    // Connect from a client stream in a separate thread
    auto payload = std::string{"hello from client"};
    auto received = std::string{};
    auto client_ok = std::atomic<bool>{false};

    auto client_thread = std::thread{[&] {
        auto stream = cppx::http::system::stream::connect("127.0.0.1", port);
        if (!stream) return;

        // Send payload
        auto bytes = cppx::http::as_bytes(payload);
        auto sr = cppx::http::system::send_all(*stream, bytes.view());
        if (!sr) return;

        // Close write side to signal EOF
        stream->close();
        client_ok.store(true);
    }};

    // Accept the connection
    auto conn = listener->accept();
    tc.check(conn.has_value(), "accept succeeded");
    if (!conn) return;

    // Read all data
    auto buf = cppx::bytes::byte_buffer{};
    auto rr = cppx::http::system::recv_all(*conn, buf);
    tc.check(rr.has_value(), "recv_all succeeded");

    received = std::string{
        reinterpret_cast<char const*>(buf.data()), buf.size()};
    tc.check(received == payload, "round-trip payload matches");

    conn->close();
    listener->close();
    client_thread.join();
    tc.check(client_ok.load(), "client thread completed ok");
}

void test_connect_refused() {
    // Connect to a port that nobody is listening on.
    // Port 1 is almost certainly unused and requires root.
    auto stream = cppx::http::system::stream::connect("127.0.0.1", 1);
    tc.check(!stream.has_value(), "connect to port 1 fails");
    tc.check(stream.error() == cppx::http::net_error::connect_refused,
          "error is connect_refused");
}

void test_dns_resolution() {
    // "localhost" should resolve to 127.0.0.1 (or ::1)
    auto listener = cppx::http::system::listener::bind("localhost", 0);
    tc.check(listener.has_value(), "bind localhost resolves");
    if (listener) {
        tc.check(listener->local_port() > 0, "localhost bound port");
        listener->close();
    }
}

void test_https_get() {
#if defined(__APPLE__)
    auto resp = cppx::http::system::get(
        "https://raw.githubusercontent.com/misut/phenotype/main/examples/native/assets/showcase.bmp",
        http_test_user_agent());
    tc.check(resp.has_value(), "HTTPS GET succeeds");
    if (resp) {
        tc.check(resp->stat.code == 200, "HTTPS status 200");
        tc.check(!resp->body.empty(), "HTTPS body non-empty");
        auto content_length = resp->hdrs.get("content-length");
        tc.check(content_length.has_value(), "macOS HTTPS GET has content-length");
        if (content_length) {
            auto parsed = parse_content_length(*content_length);
            tc.check(parsed.has_value(), "content-length parses as a number");
            if (parsed)
                tc.check(resp->body.size() == *parsed,
                      "GitHub asset body size matches content-length");
        }
    }
#else
    auto resp = cppx::http::system::get(
        "https://www.google.com/robots.txt", http_test_user_agent());
    tc.check(resp.has_value(), "HTTPS GET succeeds");
    if (resp) {
        tc.check(resp->stat.code == 200, "HTTPS status 200");
        tc.check(!resp->body.empty(), "HTTPS body non-empty");
        tc.check(resp->body_string().contains("User-agent"),
              "robots.txt contains User-agent");
    }
#endif
}

void test_https_download() {
    auto tmp = std::filesystem::temp_directory_path();
#if defined(__APPLE__)
    // GitHub release assets redirect to a keep-alive blob response with an
    // explicit Content-Length. This path exercises the macOS download stall
    // regression that simple close-delimited responses miss.
    auto path = tmp / "cppx_test_https_dl.zip";
    auto r = cppx::http::system::download(
        "https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-mac.zip",
        path, http_test_user_agent());
#else
    auto path = tmp / "cppx_test_https_dl.txt";
    auto r = cppx::http::system::download(
        "https://www.google.com/robots.txt", path, http_test_user_agent());
#endif
    tc.check(r.has_value(), "HTTPS download succeeds");
    if (r) {
        tc.check(std::filesystem::exists(path), "downloaded file exists");
        tc.check(std::filesystem::file_size(path) > 0,
              "downloaded file non-empty");
        std::filesystem::remove(path);
    }
}

int main() {
    test_tcp_roundtrip();
    test_connect_refused();
    test_dns_resolution();
    test_https_get();
    test_https_download();
    return tc.summary("cppx.http.system");
}
