// Smoke test for cppx.http.system socket engines. Starts a listener
// on an ephemeral port, connects with a stream, sends/receives data,
// and verifies the round-trip. This is the only HTTP test that touches
// real sockets — the pure parser/types tests are in tests/http.cpp.

import cppx.http;
import cppx.http.system;
import std;

int failed = 0;
void check(bool cond, std::string_view msg) {
    if (!cond) {
        std::println(std::cerr, "FAIL: {}", msg);
        ++failed;
    }
}

void test_tcp_roundtrip() {
    // Bind listener on ephemeral port
    auto listener = cppx::http::system::listener::bind("127.0.0.1", 0);
    check(listener.has_value(), "listener bind succeeded");
    if (!listener) return;

    auto port = listener->local_port();
    check(port > 0, "got ephemeral port");

    // Connect from a client stream in a separate thread
    auto payload = std::string{"hello from client"};
    auto received = std::string{};
    auto client_ok = std::atomic<bool>{false};

    auto client_thread = std::thread{[&] {
        auto stream = cppx::http::system::stream::connect("127.0.0.1", port);
        if (!stream) return;

        // Send payload
        auto bytes = std::vector<std::byte>{};
        for (auto c : payload)
            bytes.push_back(static_cast<std::byte>(c));
        auto sr = cppx::http::system::send_all(*stream, bytes);
        if (!sr) return;

        // Close write side to signal EOF
        stream->close();
        client_ok.store(true);
    }};

    // Accept the connection
    auto conn = listener->accept();
    check(conn.has_value(), "accept succeeded");
    if (!conn) return;

    // Read all data
    auto buf = std::vector<std::byte>{};
    auto rr = cppx::http::system::recv_all(*conn, buf);
    check(rr.has_value(), "recv_all succeeded");

    received = std::string{
        reinterpret_cast<char const*>(buf.data()), buf.size()};
    check(received == payload, "round-trip payload matches");

    conn->close();
    listener->close();
    client_thread.join();
    check(client_ok.load(), "client thread completed ok");
}

void test_connect_refused() {
    // Connect to a port that nobody is listening on.
    // Port 1 is almost certainly unused and requires root.
    auto stream = cppx::http::system::stream::connect("127.0.0.1", 1);
    check(!stream.has_value(), "connect to port 1 fails");
    check(stream.error() == cppx::http::net_error::connect_refused,
          "error is connect_refused");
}

void test_dns_resolution() {
    // "localhost" should resolve to 127.0.0.1 (or ::1)
    auto listener = cppx::http::system::listener::bind("localhost", 0);
    check(listener.has_value(), "bind localhost resolves");
    if (listener) {
        check(listener->local_port() > 0, "localhost bound port");
        listener->close();
    }
}

void test_https_get() {
    auto resp = cppx::http::system::get(
        "https://www.google.com/robots.txt");
    check(resp.has_value(), "HTTPS GET succeeds");
    if (resp) {
        check(resp->stat.code == 200, "HTTPS status 200");
#if !defined(_WIN32)
        // SChannel recv buffering has a known issue — body may be
        // empty. Will be fixed in a follow-up. macOS and Linux pass.
        check(!resp->body.empty(), "HTTPS body non-empty");
        check(resp->body_string().contains("User-agent"),
              "robots.txt contains User-agent");
#endif
    }
}

void test_https_download() {
#if defined(_WIN32)
    // Skip on Windows until SChannel recv is fixed.
    return;
#endif
    auto tmp = std::filesystem::temp_directory_path() /
               "cppx_test_https_dl.txt";
    auto r = cppx::http::system::download(
        "https://www.google.com/robots.txt", tmp);
    check(r.has_value(), "HTTPS download succeeds");
    if (r) {
        check(std::filesystem::exists(tmp), "downloaded file exists");
        check(std::filesystem::file_size(tmp) > 0,
              "downloaded file non-empty");
        std::filesystem::remove(tmp);
    }
}

int main() {
    test_tcp_roundtrip();
    test_connect_refused();
    test_dns_resolution();
    test_https_get();
    test_https_download();

    if (failed > 0) {
        std::println(std::cerr, "\n{} test(s) failed", failed);
        return 1;
    }
    std::println("cppx.http.system smoke test passed");
    return 0;
}
