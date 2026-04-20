// Deterministic machine-local tests for cppx.http.system. Public
// internet coverage lives in tests/http_system_smoke.cpp.

import cppx.http;
import cppx.http.server;
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

auto read_text_file(std::filesystem::path const& path) -> std::string {
    auto in = std::ifstream{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
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

void test_local_get() {
    auto listener = cppx::http::system::listener::bind("127.0.0.1", 0);
    tc.check(listener.has_value(), "local GET listener bind succeeds");
    if (!listener)
        return;

    auto port = listener->local_port();
    auto ready = std::promise<void>{};
    auto ready_fut = ready.get_future();
    auto server_thread = std::thread{[&, l = std::move(*listener)]() mutable {
        ready.set_value();
        auto conn = l.accept();
        if (!conn)
            return;
        cppx::http::detail::handle_connection(
            std::move(*conn),
            {{cppx::http::method::GET, "/health",
              [](cppx::http::request const& req) -> cppx::http::response {
                  auto resp = cppx::http::response{
                      .stat = {200},
                      .hdrs = {},
                      .body = cppx::http::as_bytes("local ok"),
                  };
                  if (auto user_agent = req.hdrs.get("user-agent"))
                      resp.hdrs.set("x-seen-user-agent", *user_agent);
                  return resp;
              }}},
            [](cppx::http::request const&) -> cppx::http::response {
                return {.stat = {404}, .hdrs = {}, .body = {}};
            });
        l.close();
    }};

    ready_fut.wait();

    auto resp = cppx::http::system::get(
        std::format("http://127.0.0.1:{}/health", port),
        http_test_user_agent());

    tc.check(resp.has_value(), "local GET succeeds");
    if (resp) {
        tc.check(resp->stat.code == 200, "local GET status 200");
        tc.check(resp->body_string() == "local ok", "local GET body matches");
        tc.check(resp->hdrs.get("x-seen-user-agent")
                     == std::optional<std::string_view>{"cppx/test-http_system"},
                 "local GET forwards request headers");
    }

    server_thread.join();
}

void test_local_download() {
    auto listener = cppx::http::system::listener::bind("127.0.0.1", 0);
    tc.check(listener.has_value(), "local download listener bind succeeds");
    if (!listener)
        return;

    auto port = listener->local_port();
    auto ready = std::promise<void>{};
    auto ready_fut = ready.get_future();
    auto server_thread = std::thread{[&, l = std::move(*listener)]() mutable {
        ready.set_value();
        auto conn = l.accept();
        if (!conn)
            return;
        cppx::http::detail::handle_connection(
            std::move(*conn),
            {{cppx::http::method::GET, "/file",
              [](cppx::http::request const&) -> cppx::http::response {
                  return {
                      .stat = {200},
                      .hdrs = {},
                      .body = cppx::http::as_bytes("downloaded locally"),
                  };
              }}},
            [](cppx::http::request const&) -> cppx::http::response {
                return {.stat = {404}, .hdrs = {}, .body = {}};
            });
        l.close();
    }};

    ready_fut.wait();

    auto path = std::filesystem::temp_directory_path() / std::format(
        "cppx-http-system-local-{}",
        std::chrono::steady_clock::now().time_since_epoch().count());
    auto result = cppx::http::system::download(
        std::format("http://127.0.0.1:{}/file", port),
        path,
        http_test_user_agent());

    tc.check(result.has_value(), "local download succeeds");
    if (result) {
        tc.check(std::filesystem::exists(path), "local download file exists");
        tc.check(read_text_file(path) == "downloaded locally",
                 "local download contents match");
    }

    std::filesystem::remove(path);
    server_thread.join();
}

int main() {
    test_tcp_roundtrip();
    test_connect_refused();
    test_dns_resolution();
    test_local_get();
    test_local_download();
    return tc.summary("cppx.http.system");
}
