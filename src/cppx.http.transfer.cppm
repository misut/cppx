// Transfer policy types shared by cppx.http and shell-backed fallbacks.
// Backend execution stays in cppx.http.transfer.system.

export module cppx.http.transfer;
import std;
import cppx.http;

export namespace cppx::http::transfer {

enum class TransferBackend {
    Auto,
    CppxHttp,
    Shell,
};

inline constexpr auto to_string(TransferBackend backend) -> std::string_view {
    switch (backend) {
    case TransferBackend::Auto:
        return "auto";
    case TransferBackend::CppxHttp:
        return "cppx.http";
    case TransferBackend::Shell:
        return "shell";
    }
    return "auto";
}

struct TransferOptions {
    TransferBackend backend = TransferBackend::Auto;
    cppx::http::headers headers;
    std::chrono::milliseconds shell_timeout = std::chrono::seconds{60};
};

struct TransferResult {
    TransferBackend backend = TransferBackend::CppxHttp;
    std::optional<std::string> warning;
};

struct TextResult {
    TransferBackend backend = TransferBackend::CppxHttp;
    std::optional<std::string> warning;
    std::string text;
};

enum class transfer_error_code {
    http_failure,
    shell_unavailable,
    shell_failed,
    read_failed,
    unsupported,
};

inline constexpr auto to_string(transfer_error_code code) -> std::string_view {
    switch (code) {
    case transfer_error_code::http_failure:
        return "http_failure";
    case transfer_error_code::shell_unavailable:
        return "shell_unavailable";
    case transfer_error_code::shell_failed:
        return "shell_failed";
    case transfer_error_code::read_failed:
        return "read_failed";
    case transfer_error_code::unsupported:
        return "unsupported";
    }
    return "http_failure";
}

struct TransferError {
    transfer_error_code code = transfer_error_code::http_failure;
    TransferBackend backend = TransferBackend::CppxHttp;
    std::string message;
    std::optional<cppx::http::http_error> http_error;
    std::optional<std::uint16_t> http_status_code;
    bool fallback_allowed = false;
};

inline auto should_shell_fallback(cppx::http::http_error error) -> bool {
    return error == cppx::http::http_error::connection_failed ||
        error == cppx::http::http_error::tls_failed ||
        error == cppx::http::http_error::response_parse_failed ||
        error == cppx::http::http_error::timeout;
}

} // namespace cppx::http::transfer
