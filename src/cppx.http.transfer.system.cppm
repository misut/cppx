export module cppx.http.transfer.system;
import std;
import cppx.fs.system;
import cppx.http;
import cppx.http.system;
import cppx.http.transfer;
import cppx.process;
import cppx.process.system;

namespace cppx::http::transfer::detail {

inline auto make_error(cppx::http::transfer::transfer_error_code code,
                       cppx::http::transfer::TransferBackend backend,
                       std::string message,
                       std::optional<cppx::http::http_error> http_error = std::nullopt,
                       std::optional<std::uint16_t> http_status_code = std::nullopt,
                       bool fallback_allowed = false)
    -> std::unexpected<cppx::http::transfer::TransferError> {
    return std::unexpected(cppx::http::transfer::TransferError{
        .code = code,
        .backend = backend,
        .message = std::move(message),
        .http_error = http_error,
        .http_status_code = http_status_code,
        .fallback_allowed = fallback_allowed,
    });
}

inline auto ensure_parent_directory(std::filesystem::path const& path)
    -> std::expected<void, cppx::http::transfer::TransferError> {
    auto parent = path.parent_path();
    if (parent.empty())
        return {};

    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        return make_error(
            cppx::http::transfer::transfer_error_code::read_failed,
            cppx::http::transfer::TransferBackend::Shell,
            std::format("could not create directory '{}': {}", parent.string(), ec.message()));
    }
    return {};
}

inline auto cleanup_download_target(std::filesystem::path const& path) -> void {
    auto partial = path;
    partial += ".part";

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(partial, ec);
}

inline auto trim_line_endings(std::string_view text) -> std::string {
    auto trimmed = std::string{text};
    while (!trimmed.empty() &&
           (trimmed.back() == '\r' || trimmed.back() == '\n')) {
        trimmed.pop_back();
    }
    return trimmed;
}

inline auto powershell_quote(std::string_view value) -> std::string {
    auto out = std::string{"'"};
    for (auto ch : value) {
        if (ch == '\'')
            out += "''";
        else
            out += ch;
    }
    out += '\'';
    return out;
}

inline auto temp_file_path(std::string_view prefix, std::string_view suffix)
    -> std::filesystem::path {
    static auto counter = std::atomic<std::uint64_t>{0};
    return std::filesystem::temp_directory_path() /
        std::format(
            "cppx-transfer-{}-{}-{}{}",
            prefix,
            std::chrono::steady_clock::now().time_since_epoch().count(),
            counter.fetch_add(1, std::memory_order_relaxed),
            suffix);
}

inline auto http_failure(std::string_view operation, cppx::http::http_error error)
    -> cppx::http::transfer::TransferError {
    return cppx::http::transfer::TransferError{
        .code = cppx::http::transfer::transfer_error_code::http_failure,
        .backend = cppx::http::transfer::TransferBackend::CppxHttp,
        .message = std::format("{} failed: {}", operation, cppx::http::to_string(error)),
        .http_error = error,
        .fallback_allowed = cppx::http::transfer::should_shell_fallback(error),
    };
}

inline auto http_status_failure(std::uint16_t status_code)
    -> cppx::http::transfer::TransferError {
    return cppx::http::transfer::TransferError{
        .code = cppx::http::transfer::transfer_error_code::http_failure,
        .backend = cppx::http::transfer::TransferBackend::CppxHttp,
        .message = std::format("HTTP {}", status_code),
        .http_status_code = status_code,
        .fallback_allowed = false,
    };
}

inline auto run_shell(cppx::process::ProcessSpec spec,
                      cppx::http::transfer::TransferOptions const& options,
                      std::string_view unavailable_message,
                      std::string_view operation)
    -> std::expected<cppx::process::CapturedProcessResult, cppx::http::transfer::TransferError> {
    spec.timeout = options.shell_timeout;
    auto const timeout = spec.timeout;
    auto result = cppx::process::system::capture(std::move(spec));
    if (!result) {
        auto code = result.error() == cppx::process::process_error::spawn_failed
            ? cppx::http::transfer::transfer_error_code::shell_unavailable
            : cppx::http::transfer::transfer_error_code::shell_failed;
        return make_error(
            code,
            cppx::http::transfer::TransferBackend::Shell,
            result.error() == cppx::process::process_error::spawn_failed
                ? std::string{unavailable_message}
                : std::format(
                      "{} failed: {}",
                      operation,
                      cppx::process::to_string(result.error())));
    }
    if (result->timed_out) {
        return make_error(
            cppx::http::transfer::transfer_error_code::shell_failed,
            cppx::http::transfer::TransferBackend::Shell,
            timeout
                ? std::format(
                      "{} failed: shell backend timed out after {}ms",
                      operation,
                      timeout->count())
                : std::format("{} failed: shell backend timed out", operation));
    }
    if (result->exit_code != 0) {
        auto stderr_text = trim_line_endings(result->stderr_text);
        return make_error(
            cppx::http::transfer::transfer_error_code::shell_failed,
            cppx::http::transfer::TransferBackend::Shell,
            stderr_text.empty()
                ? std::format("{} failed: shell backend failed", operation)
                : std::format("{} failed: {}", operation, stderr_text));
    }
    return *result;
}

#if defined(_WIN32)
inline auto shell_text(cppx::http::transfer::TransferOptions const& options,
                       std::string_view url)
    -> std::expected<cppx::http::transfer::TextResult, cppx::http::transfer::TransferError> {
    auto script = std::string{
        "$ErrorActionPreference='Stop';"
        "$ProgressPreference='SilentlyContinue';"
        "$headers=@{};"
    };
    for (auto const& [name, value] : options.headers) {
        script += std::format(
            "$headers[{}]={};",
            powershell_quote(name),
            powershell_quote(value));
    }
    script += std::format(
        "(Invoke-WebRequest -UseBasicParsing -Uri {} -Headers $headers -MaximumRedirection 10).Content",
        powershell_quote(url));

    auto result = run_shell(
        {
            .program = "powershell",
            .args = {
                "-NoLogo",
                "-NoProfile",
                "-NonInteractive",
                "-ExecutionPolicy",
                "Bypass",
                "-Command",
                script,
            },
        },
        options,
        "request failed: shell backend unavailable (powershell not found)",
        "request");
    if (!result)
        return std::unexpected(result.error());

    return cppx::http::transfer::TextResult{
        .backend = cppx::http::transfer::TransferBackend::Shell,
        .text = std::move(result->stdout_text),
    };
}

inline auto shell_download(cppx::http::transfer::TransferOptions const& options,
                           std::string_view url,
                           std::filesystem::path const& path)
    -> std::expected<cppx::http::transfer::TransferResult, cppx::http::transfer::TransferError> {
    auto prepared = ensure_parent_directory(path);
    if (!prepared)
        return std::unexpected(prepared.error());

    auto partial = path;
    partial += ".part";
    cleanup_download_target(path);

    auto script = std::string{
        "$ErrorActionPreference='Stop';"
        "$ProgressPreference='SilentlyContinue';"
        "$headers=@{};"
    };
    for (auto const& [name, value] : options.headers) {
        script += std::format(
            "$headers[{}]={};",
            powershell_quote(name),
            powershell_quote(value));
    }
    script += std::format(
        "Invoke-WebRequest -UseBasicParsing -Uri {} -Headers $headers -OutFile {} -MaximumRedirection 10;",
        powershell_quote(url),
        powershell_quote(partial.string()));

    auto result = run_shell(
        {
            .program = "powershell",
            .args = {
                "-NoLogo",
                "-NoProfile",
                "-NonInteractive",
                "-ExecutionPolicy",
                "Bypass",
                "-Command",
                script,
            },
        },
        options,
        "download failed: shell backend unavailable (powershell not found)",
        "download");
    if (!result) {
        cleanup_download_target(path);
        return std::unexpected(result.error());
    }

    std::error_code ec;
    std::filesystem::rename(partial, path, ec);
    if (ec) {
        cleanup_download_target(path);
        return make_error(
            cppx::http::transfer::transfer_error_code::shell_failed,
            cppx::http::transfer::TransferBackend::Shell,
            std::format("download failed: could not finalize file '{}': {}", path.string(), ec.message()));
    }

    return cppx::http::transfer::TransferResult{
        .backend = cppx::http::transfer::TransferBackend::Shell,
    };
}
#else
inline auto shell_text(cppx::http::transfer::TransferOptions const& options,
                       std::string_view url)
    -> std::expected<cppx::http::transfer::TextResult, cppx::http::transfer::TransferError> {
    auto spec = cppx::process::ProcessSpec{
        .program = "curl",
        .args = {"-fsSL", "--compressed"},
    };
    for (auto const& [name, value] : options.headers) {
        spec.args.push_back("-H");
        spec.args.push_back(std::format("{}: {}", name, value));
    }
    spec.args.push_back(std::string{url});

    auto captured = run_shell(
        std::move(spec),
        options,
        "request failed: shell backend unavailable (curl not found)",
        "request");
    if (!captured)
        return std::unexpected(captured.error());

    return cppx::http::transfer::TextResult{
        .backend = cppx::http::transfer::TransferBackend::Shell,
        .text = std::move(captured->stdout_text),
    };
}

inline auto shell_download(cppx::http::transfer::TransferOptions const& options,
                           std::string_view url,
                           std::filesystem::path const& path)
    -> std::expected<cppx::http::transfer::TransferResult, cppx::http::transfer::TransferError> {
    auto prepared = ensure_parent_directory(path);
    if (!prepared)
        return std::unexpected(prepared.error());

    auto partial = path;
    partial += ".part";
    cleanup_download_target(path);

    auto spec = cppx::process::ProcessSpec{
        .program = "curl",
        .args = {"-fSL", "--compressed"},
    };
    for (auto const& [name, value] : options.headers) {
        spec.args.push_back("-H");
        spec.args.push_back(std::format("{}: {}", name, value));
    }
    spec.args.push_back("-o");
    spec.args.push_back(partial.string());
    spec.args.push_back(std::string{url});

    auto captured = run_shell(
        std::move(spec),
        options,
        "download failed: shell backend unavailable (curl not found)",
        "download");
    if (!captured) {
        cleanup_download_target(path);
        return std::unexpected(captured.error());
    }

    std::error_code ec;
    std::filesystem::rename(partial, path, ec);
    if (ec) {
        cleanup_download_target(path);
        return make_error(
            cppx::http::transfer::transfer_error_code::shell_failed,
            cppx::http::transfer::TransferBackend::Shell,
            std::format("download failed: could not finalize file '{}': {}", path.string(), ec.message()));
    }

    return cppx::http::transfer::TransferResult{
        .backend = cppx::http::transfer::TransferBackend::Shell,
    };
}
#endif

inline auto cppx_text(cppx::http::transfer::TransferOptions const& options,
                      std::string_view url)
    -> std::expected<cppx::http::transfer::TextResult, cppx::http::transfer::TransferError> {
    auto response = cppx::http::system::client{}.get(url, options.headers);
    if (!response)
        return std::unexpected(http_failure("request", response.error()));
    if (!response->stat.ok())
        return std::unexpected(http_status_failure(response->stat.code));
    return cppx::http::transfer::TextResult{
        .backend = cppx::http::transfer::TransferBackend::CppxHttp,
        .text = response->body_string(),
    };
}

inline auto cppx_download(cppx::http::transfer::TransferOptions const& options,
                          std::string_view url,
                          std::filesystem::path const& path)
    -> std::expected<cppx::http::transfer::TransferResult, cppx::http::transfer::TransferError> {
    auto prepared = ensure_parent_directory(path);
    if (!prepared)
        return std::unexpected(prepared.error());

    auto last_error = cppx::http::transfer::TransferError{};
    for (int attempt = 1; attempt <= 3; ++attempt) {
        cleanup_download_target(path);
        auto response = cppx::http::system::client{}.download_to(url, path, options.headers);
        if (response) {
            if (!response->stat.ok()) {
                cleanup_download_target(path);
                return std::unexpected(http_status_failure(response->stat.code));
            }
            return cppx::http::transfer::TransferResult{
                .backend = cppx::http::transfer::TransferBackend::CppxHttp,
            };
        }

        last_error = http_failure("download", response.error());
        if (!last_error.fallback_allowed || attempt == 3)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds{250 * attempt});
    }

    cleanup_download_target(path);
    return std::unexpected(last_error);
}

inline auto auto_text(cppx::http::transfer::TransferOptions options,
                      std::string_view url)
    -> std::expected<cppx::http::transfer::TextResult, cppx::http::transfer::TransferError> {
    auto primary = cppx_text(options, url);
    if (primary)
        return primary;
    if (!primary.error().fallback_allowed)
        return std::unexpected(primary.error());

    auto fallback = shell_text(options, url);
    if (!fallback) {
        return make_error(
            fallback.error().code,
            fallback.error().backend,
            std::format("{}; {}", primary.error().message, fallback.error().message),
            primary.error().http_error,
            primary.error().http_status_code,
            false);
    }

    fallback->warning = std::format(
        "warning: cppx.http request failed ({}); using shell backend",
        primary.error().message);
    return fallback;
}

inline auto auto_download(cppx::http::transfer::TransferOptions options,
                          std::string_view url,
                          std::filesystem::path const& path)
    -> std::expected<cppx::http::transfer::TransferResult, cppx::http::transfer::TransferError> {
    auto primary = cppx_download(options, url, path);
    if (primary)
        return primary;
    if (!primary.error().fallback_allowed)
        return std::unexpected(primary.error());

    auto fallback = shell_download(options, url, path);
    if (!fallback) {
        return make_error(
            fallback.error().code,
            fallback.error().backend,
            std::format("{}; {}", primary.error().message, fallback.error().message),
            primary.error().http_error,
            primary.error().http_status_code,
            false);
    }

    fallback->warning = std::format(
        "warning: cppx.http download failed ({}); using shell backend",
        primary.error().message);
    return fallback;
}

} // namespace cppx::http::transfer::detail

export namespace cppx::http::transfer::system {

inline auto get_text(std::string_view url,
                     cppx::http::transfer::TransferOptions options = {})
    -> std::expected<cppx::http::transfer::TextResult, cppx::http::transfer::TransferError> {
    switch (options.backend) {
    case cppx::http::transfer::TransferBackend::CppxHttp:
        return cppx::http::transfer::detail::cppx_text(options, url);
    case cppx::http::transfer::TransferBackend::Shell:
        return cppx::http::transfer::detail::shell_text(options, url);
    case cppx::http::transfer::TransferBackend::Auto:
        return cppx::http::transfer::detail::auto_text(options, url);
    }
    return cppx::http::transfer::detail::make_error(
        cppx::http::transfer::transfer_error_code::unsupported,
        cppx::http::transfer::TransferBackend::Auto,
        "unsupported transfer backend");
}

inline auto download_file(
    std::string_view url,
    std::filesystem::path const& path,
    cppx::http::transfer::TransferOptions options = {})
    -> std::expected<cppx::http::transfer::TransferResult, cppx::http::transfer::TransferError> {
    switch (options.backend) {
    case cppx::http::transfer::TransferBackend::CppxHttp:
        return cppx::http::transfer::detail::cppx_download(options, url, path);
    case cppx::http::transfer::TransferBackend::Shell:
        return cppx::http::transfer::detail::shell_download(options, url, path);
    case cppx::http::transfer::TransferBackend::Auto:
        return cppx::http::transfer::detail::auto_download(options, url, path);
    }
    return cppx::http::transfer::detail::make_error(
        cppx::http::transfer::transfer_error_code::unsupported,
        cppx::http::transfer::TransferBackend::Auto,
        "unsupported transfer backend");
}

} // namespace cppx::http::transfer::system
