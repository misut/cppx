import cppx.http;
import cppx.http.transfer;
import cppx.test;
import std;

cppx::test::context tc;

void test_transfer_backend_to_string() {
    tc.check(cppx::http::transfer::to_string(
                 cppx::http::transfer::TransferBackend::Auto) == "auto",
             "auto backend string");
    tc.check(cppx::http::transfer::to_string(
                 cppx::http::transfer::TransferBackend::CppxHttp) == "cppx.http",
             "cppx.http backend string");
    tc.check(cppx::http::transfer::to_string(
                 cppx::http::transfer::TransferBackend::Shell) == "shell",
             "shell backend string");
}

void test_should_shell_fallback_matches_policy() {
    tc.check(cppx::http::transfer::should_shell_fallback(
                 cppx::http::http_error::connection_failed),
             "connection_failed allows fallback");
    tc.check(cppx::http::transfer::should_shell_fallback(
                 cppx::http::http_error::tls_failed),
             "tls_failed allows fallback");
    tc.check(cppx::http::transfer::should_shell_fallback(
                 cppx::http::http_error::response_parse_failed),
             "response_parse_failed allows fallback");
    tc.check(cppx::http::transfer::should_shell_fallback(
                 cppx::http::http_error::timeout),
             "timeout allows fallback");

    tc.check(!cppx::http::transfer::should_shell_fallback(
                 cppx::http::http_error::url_parse_failed),
             "url_parse_failed forbids fallback");
    tc.check(!cppx::http::transfer::should_shell_fallback(
                 cppx::http::http_error::redirect_limit),
             "redirect_limit forbids fallback");
    tc.check(!cppx::http::transfer::should_shell_fallback(
                 cppx::http::http_error::body_too_large),
             "body_too_large forbids fallback");
    tc.check(!cppx::http::transfer::should_shell_fallback(
                 cppx::http::http_error::send_failed),
             "send_failed forbids fallback");
}

void test_transfer_error_shape() {
    auto error = cppx::http::transfer::TransferError{
        .code = cppx::http::transfer::transfer_error_code::shell_failed,
        .backend = cppx::http::transfer::TransferBackend::Shell,
        .message = "curl failed",
        .http_error = cppx::http::http_error::connection_failed,
        .fallback_allowed = false,
    };

    tc.check(error.code == cppx::http::transfer::transfer_error_code::shell_failed,
             "transfer error code stored");
    tc.check(error.backend == cppx::http::transfer::TransferBackend::Shell,
             "transfer error backend stored");
    tc.check(error.message == "curl failed", "transfer error message stored");
    tc.check(error.http_error == cppx::http::http_error::connection_failed,
             "transfer error http error stored");
}

int main() {
    test_transfer_backend_to_string();
    test_should_shell_fallback_matches_policy();
    test_transfer_error_shape();
    return tc.summary("cppx.http.transfer");
}
