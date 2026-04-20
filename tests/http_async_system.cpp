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

auto http_test_user_agent() -> cppx::http::headers {
    auto hdrs = cppx::http::headers{};
    hdrs.set("user-agent", "cppx/test-http_async_system");
    return hdrs;
}

auto read_text_file(std::filesystem::path const& path) -> std::string {
    auto in = std::ifstream{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

auto parse_content_length(std::string_view value) -> std::optional<std::size_t> {
    if (value.empty())
        return std::nullopt;

    auto length = std::size_t{0};
    for (auto ch : value) {
        if (ch < '0' || ch > '9')
            return std::nullopt;
        length = length * 10 + static_cast<std::size_t>(ch - '0');
    }
    return length;
}

void test_https_get_smoke() {
#if defined(__APPLE__)
    auto task = cppx::http::async::system::get(
        "https://raw.githubusercontent.com/misut/phenotype/main/examples/native/assets/showcase.bmp",
        http_test_user_agent());
#else
    auto task = cppx::http::async::system::get(
        "https://www.google.com/robots.txt",
        http_test_user_agent());
#endif

    auto resp = cppx::async::system::run(task);
    tc.check(resp.has_value(), "async system HTTPS GET succeeds");
    if (!resp)
        return;

    tc.check(resp->stat.code == 200, "async system HTTPS GET status 200");
    tc.check(!resp->body.empty(), "async system HTTPS GET body non-empty");
#if defined(__APPLE__)
    auto content_length = resp->hdrs.get("content-length");
    tc.check(content_length.has_value(),
             "async system macOS HTTPS GET has content-length");
    if (content_length) {
        auto parsed = parse_content_length(*content_length);
        tc.check(parsed.has_value(),
                 "async system macOS content-length parses");
        if (parsed) {
            tc.check(resp->body.size() == *parsed,
                     "async system macOS body size matches content-length");
        }
    }
#else
    tc.check(resp->body_string().contains("User-agent"),
             "async system robots.txt contains User-agent");
#endif
}

void test_https_download_smoke() {
    auto tmp = std::filesystem::temp_directory_path();
#if defined(__APPLE__)
    auto path = tmp / "cppx_test_async_https_dl.zip";
    auto task = cppx::http::async::system::download(
        "https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-mac.zip",
        path,
        http_test_user_agent());
#else
    auto path = tmp / "cppx_test_async_https_dl.txt";
    auto task = cppx::http::async::system::download(
        "https://www.google.com/robots.txt",
        path,
        http_test_user_agent());
#endif

    auto result = cppx::async::system::run(task);
    tc.check(result.has_value(), "async system HTTPS download succeeds");
    if (result) {
        tc.check(std::filesystem::exists(path),
                 "async system HTTPS download file exists");
        tc.check(std::filesystem::file_size(path) > 0,
                 "async system HTTPS download file non-empty");
        std::filesystem::remove(path);
    }
}

void test_get_smoke() {
    auto listener = cppx::http::system::listener::bind("127.0.0.1", 0);
    tc.check(listener.has_value(), "async system smoke bind");
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
                      .body = cppx::http::as_bytes("async ok"),
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

    auto url = std::format("http://127.0.0.1:{}/health", port);
    auto task = cppx::http::async::system::get(url, http_test_user_agent());
    auto resp = cppx::async::system::run(task);

    tc.check(resp.has_value(), "async system GET succeeds");
    if (resp) {
        tc.check(resp->stat.code == 200, "async system GET status 200");
        tc.check(resp->body_string() == "async ok", "async system GET body");
        tc.check(resp->hdrs.get("x-seen-user-agent")
                     == std::optional<std::string_view>{"cppx/test-http_async_system"},
                 "async system GET forwards request headers");
    }

    server_thread.join();
}

void test_download_smoke() {
    auto listener = cppx::http::system::listener::bind("127.0.0.1", 0);
    tc.check(listener.has_value(), "async system download bind");
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
                      .body = cppx::http::as_bytes("downloaded"),
                  };
              }}},
            [](cppx::http::request const&) -> cppx::http::response {
                return {.stat = {404}, .hdrs = {}, .body = {}};
            });
        l.close();
    }};

    ready_fut.wait();

    auto path = std::filesystem::temp_directory_path() / std::format(
        "cppx-http-async-system-{}",
        std::chrono::steady_clock::now().time_since_epoch().count());
    auto url = std::format("http://127.0.0.1:{}/file", port);
    auto task = cppx::http::async::system::download(
        url, path, http_test_user_agent());
    auto result = cppx::async::system::run(task);

    tc.check(result.has_value(), "async system download succeeds");
    if (result) {
        tc.check(read_text_file(path) == "downloaded",
                 "async system download contents");
    }
    std::filesystem::remove(path);
    server_thread.join();
}

#endif // !__wasi__

int main() {
#if !defined(__wasi__)
    if (!smoke_enabled()) {
        std::println(
            "cppx.http.async.system smoke skipped (set CPPX_RUN_HTTP_ASYNC_SYSTEM_SMOKE=1 to enable)");
        return tc.summary("cppx.http.async.system");
    }

    test_https_get_smoke();
    test_https_download_smoke();
    test_get_smoke();
    test_download_smoke();
#endif
    return tc.summary("cppx.http.async.system");
}
