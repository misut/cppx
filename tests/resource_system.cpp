import cppx.bytes;
import cppx.fs;
import cppx.fs.system;
import cppx.http;
import cppx.http.server;
import cppx.http.system;
import cppx.resource;
import cppx.resource.system;
import cppx.test;
import std;

cppx::test::context tc;

namespace {

auto same_bytes(cppx::bytes::bytes_view lhs, cppx::bytes::bytes_view rhs) -> bool {
    if (lhs.size() != rhs.size())
        return false;
    for (auto i = std::size_t{0}; i < lhs.size(); ++i) {
        if (lhs.data()[i] != rhs.data()[i])
            return false;
    }
    return true;
}

auto text_bytes(std::string_view text) -> cppx::bytes::byte_buffer {
    return cppx::bytes::byte_buffer{
        cppx::bytes::bytes_view{
            std::as_bytes(std::span{text.data(), text.size()})}};
}

auto bytes_to_string(cppx::bytes::bytes_view bytes) -> std::string {
    return std::string{
        reinterpret_cast<char const*>(bytes.data()),
        bytes.size()};
}

auto make_temp_root(std::string_view name) -> std::filesystem::path {
    return std::filesystem::temp_directory_path() / std::format(
        "{}-{}",
        name,
        std::chrono::steady_clock::now().time_since_epoch().count());
}

auto percent_encode_path(std::string_view value) -> std::string {
    auto encoded = std::string{};
    for (auto ch : value) {
        auto const byte = static_cast<unsigned char>(ch);
        if (std::isalnum(byte)
            || ch == '/'
            || ch == '-'
            || ch == '_'
            || ch == '.'
            || ch == '~'
            || ch == ':') {
            encoded.push_back(ch);
            continue;
        }
        encoded += std::format("%{:02X}", byte);
    }
    return encoded;
}

auto file_uri_from_path(std::filesystem::path const& path,
                        std::string_view authority = {})
    -> std::string {
    auto encoded = percent_encode_path(path.generic_string());
#if defined(_WIN32)
    encoded.insert(encoded.begin(), '/');
#endif
    if (authority.empty())
        return std::format("file://{}", encoded);
    return std::format("file://{}{}", authority, encoded);
}

void serve_once(cppx::http::system::listener& listener, std::string response) {
    auto conn = listener.accept();
    if (!conn)
        return;
    auto payload = text_bytes(response);
    auto sent = cppx::http::system::send_all(*conn, payload.view());
    (void)sent;
    conn->close();
}

} // namespace

void test_local_relative_path() {
    auto root = make_temp_root("cppx-resource-system-relative");
    auto file = root / "assets" / "sample.bin";
    auto payload = text_bytes("relative bytes");

    auto write = cppx::fs::system::write_bytes(file, payload.view());
    tc.check(write.has_value(), "write relative test fixture");

    auto read = cppx::resource::system::read_bytes(root, "assets/sample.bin");
    tc.check(read.has_value(), "read relative local path");
    if (read)
        tc.check(same_bytes(read->view(), payload.view()),
                 "relative local path preserves bytes");

    std::filesystem::remove_all(root);
}

void test_local_absolute_path() {
    auto root = make_temp_root("cppx-resource-system-absolute");
    auto file = root / "payload.bin";
    auto payload = text_bytes("absolute bytes");

    auto write = cppx::fs::system::write_bytes(file, payload.view());
    tc.check(write.has_value(), "write absolute test fixture");

    auto read = cppx::resource::system::read_bytes(root, file.string());
    tc.check(read.has_value(), "read absolute local path");
    if (read)
        tc.check(same_bytes(read->view(), payload.view()),
                 "absolute local path preserves bytes");

    std::filesystem::remove_all(root);
}

void test_file_url_reads() {
    auto root = make_temp_root("cppx-resource-system-file-url");
    auto file = root / "dir with spaces" / "space file.bin";
    auto payload = text_bytes("file url bytes");

    auto write = cppx::fs::system::write_bytes(file, payload.view());
    tc.check(write.has_value(), "write file URL fixture");

    auto empty_authority = cppx::resource::system::read_bytes(
        root,
        file_uri_from_path(file));
    tc.check(empty_authority.has_value(), "read file URL with empty authority");
    if (empty_authority)
        tc.check(same_bytes(empty_authority->view(), payload.view()),
                 "file URL with empty authority preserves bytes");

    auto localhost = cppx::resource::system::read_bytes(
        root,
        file_uri_from_path(file, "localhost"));
    tc.check(localhost.has_value(), "read file URL with localhost authority");
    if (localhost)
        tc.check(same_bytes(localhost->view(), payload.view()),
                 "file URL with localhost authority preserves bytes");

    std::filesystem::remove_all(root);
}

#if !defined(__wasi__)
void test_remote_url_and_headers() {
    auto listener = cppx::http::system::listener::bind("127.0.0.1", 0);
    tc.check(listener.has_value(), "remote resource listener bind");
    if (!listener)
        return;

    auto const port = listener->local_port();
    auto ready = std::promise<void>{};
    auto ready_fut = ready.get_future();
    auto observed_header = std::optional<std::string>{};
    auto server_done = std::atomic<bool>{false};

    auto server_thread = std::thread{[&,
                                      l = std::move(*listener)]() mutable {
        ready.set_value();
        auto conn = l.accept();
        if (!conn)
            return;

        cppx::http::detail::handle_connection(
            std::move(*conn),
            {{
                cppx::http::method::GET,
                "/asset",
                [&](cppx::http::request const& req) -> cppx::http::response {
                    if (auto value = req.hdrs.get("x-cppx-test"))
                        observed_header = std::string{*value};
                    return {
                        .stat = {200},
                        .hdrs = {},
                        .body = cppx::http::as_bytes("remote bytes"),
                    };
                },
            }},
            [](cppx::http::request const&) -> cppx::http::response {
                return {.stat = {404}, .hdrs = {}, .body = {}};
            });
        l.close();
        server_done.store(true);
    }};

    ready_fut.wait();

    auto headers = cppx::http::headers{};
    headers.set("x-cppx-test", "resource-system");
    auto read = cppx::resource::system::read_bytes(
        std::filesystem::temp_directory_path(),
        std::format("http://127.0.0.1:{}/asset", port),
        std::move(headers));

    tc.check(read.has_value(), "read remote URL");
    if (read)
        tc.check(bytes_to_string(read->view()) == "remote bytes",
                 "remote URL preserves response body");

    server_thread.join();
    tc.check(server_done.load(), "remote resource server completed");
    tc.check(observed_header == std::optional<std::string>{"resource-system"},
             "remote resource forwards request headers");
}

void test_remote_http_404_mapping() {
    auto listener = cppx::http::system::listener::bind("127.0.0.1", 0);
    tc.check(listener.has_value(), "404 listener bind");
    if (!listener)
        return;

    auto server_thread = std::thread{[&] {
        serve_once(
            *listener,
            "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    }};

    auto read = cppx::resource::system::read_bytes(
        std::filesystem::temp_directory_path(),
        std::format("http://127.0.0.1:{}/missing", listener->local_port()));

    tc.check(!read.has_value(), "404 result surfaces as error");
    if (!read) {
        tc.check(read.error().code
                     == cppx::resource::system::resource_read_error_code::not_found,
                 "404 maps to not_found");
        tc.check(read.error().http_status_code == 404,
                 "404 status code preserved");
    }

    listener->close();
    server_thread.join();
}

void test_remote_http_500_mapping() {
    auto listener = cppx::http::system::listener::bind("127.0.0.1", 0);
    tc.check(listener.has_value(), "500 listener bind");
    if (!listener)
        return;

    auto server_thread = std::thread{[&] {
        serve_once(
            *listener,
            "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    }};

    auto read = cppx::resource::system::read_bytes(
        std::filesystem::temp_directory_path(),
        std::format("http://127.0.0.1:{}/error", listener->local_port()));

    tc.check(!read.has_value(), "500 result surfaces as error");
    if (!read) {
        tc.check(read.error().code
                     == cppx::resource::system::resource_read_error_code::read_failed,
                 "500 maps to read_failed");
        tc.check(read.error().http_status_code == 500,
                 "500 status code preserved");
    }

    listener->close();
    server_thread.join();
}

void test_remote_transport_failure_mapping() {
    auto read = cppx::resource::system::read_bytes(
        std::filesystem::temp_directory_path(),
        "http://127.0.0.1:1/unreachable");

    tc.check(!read.has_value(), "connection failure surfaces as error");
    if (!read)
        tc.check(read.error().code
                     == cppx::resource::system::resource_read_error_code::transport_failed,
                 "connection failure maps to transport_failed");
    if (!read)
        tc.check(read.error().http_error == cppx::http::http_error::connection_failed,
                 "connection failure preserves http_error");
}
#endif

void test_unsupported_locators() {
    auto base = std::filesystem::temp_directory_path();

    auto mailto = cppx::resource::system::read_bytes(
        base,
        "mailto:hello@example.com");
    tc.check(!mailto.has_value(), "mailto locator is unsupported");
    if (!mailto)
        tc.check(mailto.error().code
                     == cppx::resource::system::resource_read_error_code::unsupported,
                 "mailto maps to unsupported");

    auto non_local_file = cppx::resource::system::read_bytes(
        base,
        "file://server/share/file.bin");
    tc.check(!non_local_file.has_value(), "non-local file URL is unsupported");
    if (!non_local_file)
        tc.check(non_local_file.error().code
                     == cppx::resource::system::resource_read_error_code::unsupported,
                 "non-local file URL maps to unsupported");
}

void test_local_not_found_and_read_failure_mapping() {
    auto root = make_temp_root("cppx-resource-system-errors");
    std::filesystem::create_directories(root / "dir-only");

    auto missing = cppx::resource::system::read_bytes(root, "missing.bin");
    tc.check(!missing.has_value(), "missing local file surfaces as error");
    if (!missing) {
        tc.check(missing.error().code
                     == cppx::resource::system::resource_read_error_code::not_found,
                 "missing local file maps to not_found");
        tc.check(missing.error().fs_error == cppx::fs::fs_error::not_found,
                 "missing local file preserves fs_error");
    }

    auto directory = cppx::resource::system::read_bytes(root, "dir-only");
    tc.check(!directory.has_value(), "directory read surfaces as error");
    if (!directory) {
        tc.check(directory.error().code
                     == cppx::resource::system::resource_read_error_code::read_failed,
                 "directory read maps to read_failed");
        tc.check(directory.error().fs_error == cppx::fs::fs_error::read_failed,
                 "directory read preserves fs_error");
    }

    std::filesystem::remove_all(root);
}

int main() {
    test_local_relative_path();
    test_local_absolute_path();
    test_file_url_reads();
#if !defined(__wasi__)
    test_remote_url_and_headers();
    test_remote_http_404_mapping();
    test_remote_http_500_mapping();
    test_remote_transport_failure_mapping();
#endif
    test_unsupported_locators();
    test_local_not_found_and_read_failure_mapping();
    return tc.summary("cppx.resource.system");
}
