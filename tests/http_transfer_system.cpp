import cppx.http;
import cppx.http.system;
import cppx.http.transfer;
import cppx.http.transfer.system;
import cppx.test;
import std;

cppx::test::context tc;

namespace {

auto bytes_from(std::string_view text) -> std::vector<std::byte> {
    auto bytes = std::vector<std::byte>{};
    bytes.reserve(text.size());
    for (auto ch : text)
        bytes.push_back(static_cast<std::byte>(ch));
    return bytes;
}

void serve_once(cppx::http::system::listener& listener, std::string response) {
    auto conn = listener.accept();
    if (!conn)
        return;
    auto payload = bytes_from(response);
    auto sent = cppx::http::system::send_all(*conn, payload);
    (void)sent;
    conn->close();
}

} // namespace

void test_get_text_does_not_fallback_on_http_404() {
    auto listener = cppx::http::system::listener::bind("127.0.0.1", 0);
    tc.check(listener.has_value(), "listener bind succeeds");
    if (!listener)
        return;

    auto server = std::thread{[&] {
        serve_once(
            *listener,
            "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\nConnection: close\r\n\r\nnot found");
    }};

    auto result = cppx::http::transfer::system::get_text(
        std::format("http://127.0.0.1:{}/missing", listener->local_port()),
        {.backend = cppx::http::transfer::TransferBackend::Auto});

    tc.check(!result, "404 result surfaced as error");
    if (!result) {
        tc.check(result.error().backend ==
                     cppx::http::transfer::TransferBackend::CppxHttp,
                 "404 stays on cppx.http backend");
        tc.check(result.error().http_status_code == 404,
                 "404 status code preserved");
        tc.check(!result.error().fallback_allowed,
                 "404 does not allow fallback");
    }

    listener->close();
    server.join();
}

void test_get_text_uses_shell_fallback_on_connection_failure() {
    auto result = cppx::http::transfer::system::get_text(
        "http://127.0.0.1:1/unreachable",
        {
            .backend = cppx::http::transfer::TransferBackend::Auto,
            .shell_timeout = std::chrono::milliseconds{500},
        });

    tc.check(!result, "connection failure surfaces as error");
    if (result)
        return;

    tc.check(result.error().backend == cppx::http::transfer::TransferBackend::Shell,
             "connection failure reaches shell backend");
    tc.check(result.error().http_error == cppx::http::http_error::connection_failed,
             "primary connection failure preserved");
    tc.check(result.error().message.contains("connection_failed"),
             "combined diagnostics mention primary backend failure");
}

void test_shell_backend_timeout_surfaces_as_shell_error() {
    auto listener = cppx::http::system::listener::bind("127.0.0.1", 0);
    tc.check(listener.has_value(), "timeout listener bind succeeds");
    if (!listener)
        return;

    auto release = std::atomic<bool>{false};
    auto server = std::thread{[&] {
        auto conn = listener->accept();
        if (!conn)
            return;
        while (!release.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        conn->close();
    }};

    auto result = cppx::http::transfer::system::get_text(
        std::format("http://127.0.0.1:{}/stall", listener->local_port()),
        {
            .backend = cppx::http::transfer::TransferBackend::Shell,
            .shell_timeout = std::chrono::milliseconds{200},
        });

    release.store(true, std::memory_order_release);
    listener->close();
    server.join();

    tc.check(!result, "shell timeout surfaces as error");
    if (result)
        return;

    tc.check(result.error().backend == cppx::http::transfer::TransferBackend::Shell,
             "timeout error stays on shell backend");
    tc.check(result.error().code == cppx::http::transfer::transfer_error_code::shell_failed,
             "timeout maps to shell_failed");
    tc.check(result.error().message.contains("timed out"),
             "timeout diagnostics mention timeout");
}

void test_download_file_uses_cppx_http_without_fallback_on_http_404() {
    auto listener = cppx::http::system::listener::bind("127.0.0.1", 0);
    tc.check(listener.has_value(), "download listener bind succeeds");
    if (!listener)
        return;

    auto output = std::filesystem::temp_directory_path() / std::format(
        "cppx-transfer-{}",
        std::chrono::steady_clock::now().time_since_epoch().count());

    auto server = std::thread{[&] {
        serve_once(
            *listener,
            "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    }};

    auto result = cppx::http::transfer::system::download_file(
        std::format("http://127.0.0.1:{}/missing", listener->local_port()),
        output,
        {.backend = cppx::http::transfer::TransferBackend::Auto});

    tc.check(!result, "download 404 result surfaced as error");
    if (!result) {
        tc.check(result.error().backend ==
                     cppx::http::transfer::TransferBackend::CppxHttp,
                 "download 404 stays on cppx.http backend");
        tc.check(result.error().http_status_code == 404,
                 "download 404 status code preserved");
        tc.check(!result.error().fallback_allowed,
                 "download 404 does not allow fallback");
    }
    tc.check(!std::filesystem::exists(output), "download 404 leaves no output file");

    listener->close();
    server.join();
}

void test_download_file_connection_failure_leaves_no_partial_file() {
    auto output = std::filesystem::temp_directory_path() / std::format(
        "cppx-transfer-failure-{}",
        std::chrono::steady_clock::now().time_since_epoch().count());
    auto partial = output;
    partial += ".part";

    auto result = cppx::http::transfer::system::download_file(
        "http://127.0.0.1:1/unreachable",
        output,
        {
            .backend = cppx::http::transfer::TransferBackend::Auto,
            .shell_timeout = std::chrono::milliseconds{500},
        });

    tc.check(!result, "connection failure surfaces as download error");
    tc.check(!std::filesystem::exists(output),
             "connection failure leaves no output file");
    tc.check(!std::filesystem::exists(partial),
             "connection failure leaves no partial file");
}

int main() {
    test_get_text_does_not_fallback_on_http_404();
    test_get_text_uses_shell_fallback_on_connection_failure();
    test_shell_backend_timeout_surfaces_as_shell_error();
    test_download_file_uses_cppx_http_without_fallback_on_http_404();
    test_download_file_connection_failure_leaves_no_partial_file();
    return tc.summary("cppx.http.transfer.system");
}
