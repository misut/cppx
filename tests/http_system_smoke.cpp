// Opt-in public-network smoke tests for cppx.http.system.

import cppx.http;
import cppx.http.system;
import cppx.test;
import std;

cppx::test::context tc;

#if !defined(__wasi__)
auto smoke_enabled() -> bool {
    auto const* value = std::getenv("CPPX_RUN_HTTP_SYSTEM_SMOKE");
    return value && std::string_view{value} == "1";
}

auto http_test_user_agent() -> cppx::http::headers {
    auto hdrs = cppx::http::headers{};
    hdrs.set("user-agent", "cppx/test-http_system_smoke");
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
    auto result = cppx::http::system::download(
        "https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-mac.zip",
        path,
        http_test_user_agent());
#else
    auto path = tmp / "cppx_test_https_dl.txt";
    auto result = cppx::http::system::download(
        "https://www.google.com/robots.txt",
        path,
        http_test_user_agent());
#endif
    tc.check(result.has_value(), "HTTPS download succeeds");
    if (result) {
        tc.check(std::filesystem::exists(path), "downloaded file exists");
        tc.check(std::filesystem::file_size(path) > 0,
                 "downloaded file non-empty");
        std::filesystem::remove(path);
    }
}

#endif // !defined(__wasi__)

int main() {
#if !defined(__wasi__)
    if (!smoke_enabled()) {
        std::println(
            "cppx.http.system smoke skipped (set CPPX_RUN_HTTP_SYSTEM_SMOKE=1 to enable)");
        return 0;
    }

    test_https_get();
    test_https_download();
#endif
    return tc.summary("cppx.http.system.smoke");
}
