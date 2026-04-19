import cppx.async;
import cppx.async.system;
import cppx.http;
import cppx.http.async.system;
import cppx.http.server;
import cppx.http.system;
import cppx.test;
import std;

cppx::test::context tc;

#if !defined(__wasi__)
auto smoke_enabled() -> bool {
    auto const* value = std::getenv("CPPX_RUN_HTTP_ASYNC_SYSTEM_SMOKE");
    return value && std::string_view{value} == "1";
}

void test_https_rejected() {
    auto task = cppx::http::async::system::get("https://example.com/");
    auto resp = cppx::async::system::run(task);
    tc.check(!resp.has_value(), "async system rejects https in this pass");
    tc.check(resp.error() == cppx::http::http_error::tls_failed,
             "async system https error is tls_failed");
}

void test_get_smoke() {
    auto listener = cppx::http::system::listener::bind("127.0.0.1", 0);
    tc.check(listener.has_value(), "async system smoke bind");
    if (!listener) return;
    auto port = listener->local_port();

    auto ready = std::promise<void>{};
    auto ready_fut = ready.get_future();
    auto server_thread = std::thread{[&, l = std::move(*listener)]() mutable {
        ready.set_value();
        auto conn = l.accept();
        if (!conn) return;
        cppx::http::detail::handle_connection(
            std::move(*conn),
            {{cppx::http::method::GET, "/health",
              [](cppx::http::request const&) -> cppx::http::response {
                  return {
                      .stat = {200},
                      .hdrs = {},
                      .body = cppx::http::as_bytes("async ok"),
                  };
              }}},
            [](cppx::http::request const&) -> cppx::http::response {
                return {.stat = {404}, .hdrs = {}, .body = {}};
            });
        l.close();
    }};

    ready_fut.wait();

    auto url = std::format("http://127.0.0.1:{}/health", port);
    auto task = cppx::http::async::system::get(url);
    auto resp = cppx::async::system::run(task);

    tc.check(resp.has_value(), "async system GET succeeds");
    if (resp) {
        tc.check(resp->stat.code == 200, "async system GET status 200");
        tc.check(resp->body_string() == "async ok", "async system GET body");
    }

    server_thread.join();
}

void test_download_smoke() {
    auto listener = cppx::http::system::listener::bind("127.0.0.1", 0);
    tc.check(listener.has_value(), "async system download bind");
    if (!listener) return;
    auto port = listener->local_port();

    auto ready = std::promise<void>{};
    auto ready_fut = ready.get_future();
    auto server_thread = std::thread{[&, l = std::move(*listener)]() mutable {
        ready.set_value();
        auto conn = l.accept();
        if (!conn) return;
        cppx::http::detail::handle_connection(
            std::move(*conn),
            {{cppx::http::method::GET, "/file",
              [](cppx::http::request const&) -> cppx::http::response {
                  return {
                      .stat = {200},
                      .hdrs = {},
                      .body = cppx::http::as_bytes("downloaded"),
                  };
              }}},
            [](cppx::http::request const&) -> cppx::http::response {
                return {.stat = {404}, .hdrs = {}, .body = {}};
            });
        l.close();
    }};

    ready_fut.wait();

    auto path = std::filesystem::temp_directory_path() / "cppx_http_async_system.bin";
    auto url = std::format("http://127.0.0.1:{}/file", port);
    auto task = cppx::http::async::system::download(url, path);
    auto result = cppx::async::system::run(task);

    tc.check(result.has_value(), "async system download succeeds");
#if !defined(_WIN32)
    if (result) {
        auto in = std::ifstream{path, std::ios::binary};
        auto content = std::string{std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>()};
        tc.check(content == "downloaded", "async system download contents");
    }
#endif
    std::filesystem::remove(path);
    server_thread.join();
}

#endif // !__wasi__

int main() {
#if !defined(__wasi__)
    test_https_rejected();
    if (!smoke_enabled()) {
        std::println(
            "cppx.http.async.system smoke skipped (set CPPX_RUN_HTTP_ASYNC_SYSTEM_SMOKE=1 to enable)");
        return tc.summary("cppx.http.async.system");
    }

    test_get_smoke();
    test_download_smoke();
#endif
    return tc.summary("cppx.http.async.system");
}
