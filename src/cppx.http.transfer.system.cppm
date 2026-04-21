// Transfer facade that keeps backend-selection policy explicit. The
// public entrypoints stay small while cppx.http access, shell fallback,
// and file-finalization details live in isolated helpers below.

export module cppx.http.transfer.system;
import std;
import cppx.http;
import cppx.http.transfer;
import cppx.process;
import cppx.process.system;

#if !defined(__wasi__) && !defined(__ANDROID__)
import cppx.http.system;
#endif

namespace cppx::http::transfer::detail {

using backend_t = cppx::http::transfer::TransferBackend;
using error_code_t = cppx::http::transfer::transfer_error_code;
using error_t = cppx::http::transfer::TransferError;
using text_result_t = cppx::http::transfer::TextResult;
using transfer_result_t = cppx::http::transfer::TransferResult;
using options_t = cppx::http::transfer::TransferOptions;

inline auto make_error(error_code_t code,
                       backend_t backend,
                       std::string message,
                       std::optional<cppx::http::http_error> http_error = std::nullopt,
                       std::optional<std::uint16_t> http_status_code = std::nullopt,
                       bool fallback_allowed = false)
    -> std::unexpected<error_t> {
    return std::unexpected(error_t{
        .code = code,
        .backend = backend,
        .message = std::move(message),
        .http_error = http_error,
        .http_status_code = http_status_code,
        .fallback_allowed = fallback_allowed,
    });
}

inline auto ensure_parent_directory(std::filesystem::path const& path)
    -> std::expected<void, error_t> {
    auto parent = path.parent_path();
    if (parent.empty())
        return {};

    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        return make_error(
            error_code_t::read_failed,
            backend_t::Shell,
            std::format(
                "could not create directory '{}': {}",
                parent.string(),
                ec.message()));
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

inline auto combine_primary_and_fallback(error_t const& primary,
                                         error_t fallback)
    -> std::unexpected<error_t> {
    return make_error(
        fallback.code,
        fallback.backend,
        std::format("{}; {}", primary.message, fallback.message),
        primary.http_error,
        primary.http_status_code,
        false);
}

namespace cppx_backend {

inline auto unsupported_http(std::string_view operation) -> error_t {
    return error_t{
        .code = error_code_t::unsupported,
        .backend = backend_t::CppxHttp,
        .message = std::format(
            "{} is unavailable on wasm32-wasi because cppx.http.system has no socket backend",
            operation),
        .fallback_allowed = false,
    };
}

inline auto http_failure(std::string_view operation, cppx::http::http_error error)
    -> error_t {
    return error_t{
        .code = error_code_t::http_failure,
        .backend = backend_t::CppxHttp,
        .message = std::format(
            "{} failed: {}",
            operation,
            cppx::http::to_string(error)),
        .http_error = error,
        .fallback_allowed = cppx::http::transfer::should_shell_fallback(error),
    };
}

inline auto http_status_failure(std::uint16_t status_code) -> error_t {
    return error_t{
        .code = error_code_t::http_failure,
        .backend = backend_t::CppxHttp,
        .message = std::format("HTTP {}", status_code),
        .http_status_code = status_code,
        .fallback_allowed = false,
    };
}

template <class HttpClient>
inline auto get_text(HttpClient& http_client,
                     options_t const& options,
                     std::string_view url)
    -> std::expected<text_result_t, error_t> {
#if defined(__wasi__) || defined(__ANDROID__)
    (void)http_client;
    (void)options;
    (void)url;
    return std::unexpected(unsupported_http("request"));
#else
    auto response = http_client.get(url, options.headers);
    if (!response)
        return std::unexpected(http_failure("request", response.error()));
    if (!response->stat.ok())
        return std::unexpected(http_status_failure(response->stat.code));
    return text_result_t{
        .backend = backend_t::CppxHttp,
        .text = response->body_string(),
    };
#endif
}

inline auto get_text(options_t const& options, std::string_view url)
    -> std::expected<text_result_t, error_t> {
#if defined(__wasi__) || defined(__ANDROID__)
    (void)options;
    (void)url;
    return std::unexpected(unsupported_http("request"));
#else
    auto http_client = cppx::http::system::client{};
    return get_text(http_client, options, url);
#endif
}

template <class HttpClient>
inline auto download_file(HttpClient& http_client,
                          options_t const& options,
                          std::string_view url,
                          std::filesystem::path const& path)
    -> std::expected<transfer_result_t, error_t> {
#if defined(__wasi__) || defined(__ANDROID__)
    (void)http_client;
    (void)options;
    (void)url;
    (void)path;
    return std::unexpected(unsupported_http("download"));
#else
    auto prepared = ensure_parent_directory(path);
    if (!prepared)
        return std::unexpected(prepared.error());

    auto last_error = error_t{};
    for (int attempt = 1; attempt <= 3; ++attempt) {
        cleanup_download_target(path);
        auto response = http_client.download_to(
            url,
            path,
            options.headers);
        if (response) {
            if (!response->stat.ok()) {
                cleanup_download_target(path);
                return std::unexpected(http_status_failure(response->stat.code));
            }
            return transfer_result_t{
                .backend = backend_t::CppxHttp,
            };
        }

        last_error = http_failure("download", response.error());
        if (!last_error.fallback_allowed || attempt == 3)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds{250 * attempt});
    }

    cleanup_download_target(path);
    return std::unexpected(last_error);
#endif
}

inline auto download_file(options_t const& options,
                          std::string_view url,
                          std::filesystem::path const& path)
    -> std::expected<transfer_result_t, error_t> {
#if defined(__wasi__) || defined(__ANDROID__)
    (void)options;
    (void)url;
    (void)path;
    return std::unexpected(unsupported_http("download"));
#else
    auto http_client = cppx::http::system::client{};
    return download_file(http_client, options, url, path);
#endif
}

} // namespace cppx_backend

namespace shell_backend {

#if defined(_WIN32)
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

inline auto append_powershell_headers(std::string& script,
                                      cppx::http::headers const& headers)
    -> void {
    for (auto const& [name, value] : headers) {
        script += std::format(
            "$headers[{}]={};",
            powershell_quote(name),
            powershell_quote(value));
    }
}
#else
inline auto append_curl_headers(cppx::process::ProcessSpec& spec,
                                cppx::http::headers const& headers)
    -> void {
    for (auto const& [name, value] : headers) {
        spec.args.push_back("-H");
        spec.args.push_back(std::format("{}: {}", name, value));
    }
}
#endif

inline auto run(cppx::process::ProcessSpec spec,
                options_t const& options,
                std::string_view unavailable_message,
                std::string_view operation)
    -> std::expected<cppx::process::CapturedProcessResult, error_t> {
    spec.timeout = options.shell_timeout;
    auto const timeout = spec.timeout;
    auto result = cppx::process::system::capture(std::move(spec));
    if (!result) {
        auto code = result.error() == cppx::process::process_error::spawn_failed
            ? error_code_t::shell_unavailable
            : error_code_t::shell_failed;
        return make_error(
            code,
            backend_t::Shell,
            result.error() == cppx::process::process_error::spawn_failed
                ? std::string{unavailable_message}
                : std::format(
                      "{} failed: {}",
                      operation,
                      cppx::process::to_string(result.error())));
    }
    if (result->timed_out) {
        return make_error(
            error_code_t::shell_failed,
            backend_t::Shell,
            timeout
                ? std::format(
                      "{} failed: shell backend timed out after {}ms",
                      operation,
                      timeout->count())
                : std::format(
                      "{} failed: shell backend timed out",
                      operation));
    }
    if (result->exit_code != 0) {
        auto stderr_text = trim_line_endings(result->stderr_text);
        return make_error(
            error_code_t::shell_failed,
            backend_t::Shell,
            stderr_text.empty()
                ? std::format(
                      "{} failed: shell backend failed",
                      operation)
                : std::format(
                      "{} failed: {}",
                      operation,
                      stderr_text));
    }
    return *result;
}

#if defined(_WIN32)
inline auto get_text(options_t const& options, std::string_view url)
    -> std::expected<text_result_t, error_t> {
    auto script = std::string{
        "$ErrorActionPreference='Stop';"
        "$ProgressPreference='SilentlyContinue';"
        "$headers=@{};"
    };
    append_powershell_headers(script, options.headers);
    script += std::format(
        "(Invoke-WebRequest -UseBasicParsing -Uri {} -Headers $headers -MaximumRedirection 10).Content",
        powershell_quote(url));

    auto result = run(
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

    return text_result_t{
        .backend = backend_t::Shell,
        .text = std::move(result->stdout_text),
    };
}

inline auto download_file(options_t const& options,
                          std::string_view url,
                          std::filesystem::path const& path)
    -> std::expected<transfer_result_t, error_t> {
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
    append_powershell_headers(script, options.headers);
    script += std::format(
        "Invoke-WebRequest -UseBasicParsing -Uri {} -Headers $headers -OutFile {} -MaximumRedirection 10;",
        powershell_quote(url),
        powershell_quote(partial.string()));

    auto result = run(
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
            error_code_t::shell_failed,
            backend_t::Shell,
            std::format(
                "download failed: could not finalize file '{}': {}",
                path.string(),
                ec.message()));
    }

    return transfer_result_t{
        .backend = backend_t::Shell,
    };
}
#else
inline auto get_text(options_t const& options, std::string_view url)
    -> std::expected<text_result_t, error_t> {
    auto spec = cppx::process::ProcessSpec{
        .program = "curl",
        .args = {"-fsSL", "--compressed"},
    };
    append_curl_headers(spec, options.headers);
    spec.args.push_back(std::string{url});

    auto captured = run(
        std::move(spec),
        options,
        "request failed: shell backend unavailable (curl not found)",
        "request");
    if (!captured)
        return std::unexpected(captured.error());

    return text_result_t{
        .backend = backend_t::Shell,
        .text = std::move(captured->stdout_text),
    };
}

inline auto download_file(options_t const& options,
                          std::string_view url,
                          std::filesystem::path const& path)
    -> std::expected<transfer_result_t, error_t> {
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
    append_curl_headers(spec, options.headers);
    spec.args.push_back("-o");
    spec.args.push_back(partial.string());
    spec.args.push_back(std::string{url});

    auto captured = run(
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
            error_code_t::shell_failed,
            backend_t::Shell,
            std::format(
                "download failed: could not finalize file '{}': {}",
                path.string(),
                ec.message()));
    }

    return transfer_result_t{
        .backend = backend_t::Shell,
    };
}
#endif

} // namespace shell_backend

template <class Result, class FallbackFn>
auto with_shell_fallback(std::string_view operation,
                         std::expected<Result, error_t> primary,
                         FallbackFn&& fallback_fn)
    -> std::expected<Result, error_t> {
    if (primary)
        return primary;

    auto primary_error = primary.error();
    if (!primary_error.fallback_allowed)
        return std::unexpected(primary_error);

    auto fallback = fallback_fn();
    if (!fallback)
        return combine_primary_and_fallback(primary_error, fallback.error());

    fallback->warning = std::format(
        "warning: cppx.http {} failed ({}); using shell backend",
        operation,
        primary_error.message);
    return fallback;
}

inline auto dispatch_get_text(std::string_view url, options_t options)
    -> std::expected<text_result_t, error_t> {
    switch (options.backend) {
    case backend_t::CppxHttp:
        return cppx_backend::get_text(options, url);
    case backend_t::Shell:
        return shell_backend::get_text(options, url);
    case backend_t::Auto:
        return with_shell_fallback(
            "request",
            cppx_backend::get_text(options, url),
            [&] { return shell_backend::get_text(options, url); });
    }
    return make_error(
        error_code_t::unsupported,
        backend_t::Auto,
        "unsupported transfer backend");
}

inline auto dispatch_download_file(std::string_view url,
                                   std::filesystem::path const& path,
                                   options_t options)
    -> std::expected<transfer_result_t, error_t> {
    switch (options.backend) {
    case backend_t::CppxHttp:
        return cppx_backend::download_file(options, url, path);
    case backend_t::Shell:
        return shell_backend::download_file(options, url, path);
    case backend_t::Auto:
        return with_shell_fallback(
            "download",
            cppx_backend::download_file(options, url, path),
            [&] { return shell_backend::download_file(options, url, path); });
    }
    return make_error(
        error_code_t::unsupported,
        backend_t::Auto,
        "unsupported transfer backend");
}

} // namespace cppx::http::transfer::detail

export namespace cppx::http::transfer::system {

template <class HttpClient>
    requires requires(HttpClient& http_client,
                      std::string_view url,
                      std::filesystem::path const& path,
                      cppx::http::headers headers) {
        { http_client.get(url, headers) }
            -> std::same_as<
                std::expected<cppx::http::response, cppx::http::http_error>>;
        { http_client.download_to(url, path, headers) }
            -> std::same_as<
                std::expected<cppx::http::response, cppx::http::http_error>>;
    }
inline auto get_text(
    std::string_view url,
    HttpClient& http_client,
    cppx::http::transfer::TransferOptions options = {})
    -> std::expected<
        cppx::http::transfer::TextResult,
        cppx::http::transfer::TransferError> {
    using backend_t = cppx::http::transfer::TransferBackend;
    using error_code_t = cppx::http::transfer::transfer_error_code;

    switch (options.backend) {
    case backend_t::CppxHttp:
        return cppx::http::transfer::detail::cppx_backend::get_text(
            http_client,
            options,
            url);
    case backend_t::Shell:
        return cppx::http::transfer::detail::shell_backend::get_text(options, url);
    case backend_t::Auto:
        return cppx::http::transfer::detail::with_shell_fallback(
            "request",
            cppx::http::transfer::detail::cppx_backend::get_text(
                http_client,
                options,
                url),
            [&] {
                return cppx::http::transfer::detail::shell_backend::get_text(
                    options,
                    url);
            });
    }

    return cppx::http::transfer::detail::make_error(
        error_code_t::unsupported,
        backend_t::Auto,
        "unsupported transfer backend");
}

inline auto get_text(
    std::string_view url,
    cppx::http::transfer::TransferOptions options = {})
    -> std::expected<
        cppx::http::transfer::TextResult,
        cppx::http::transfer::TransferError> {
    return cppx::http::transfer::detail::dispatch_get_text(url, options);
}

template <class HttpClient>
    requires requires(HttpClient& http_client,
                      std::string_view url,
                      std::filesystem::path const& path,
                      cppx::http::headers headers) {
        { http_client.get(url, headers) }
            -> std::same_as<
                std::expected<cppx::http::response, cppx::http::http_error>>;
        { http_client.download_to(url, path, headers) }
            -> std::same_as<
                std::expected<cppx::http::response, cppx::http::http_error>>;
    }
inline auto download_file(std::string_view url,
                          std::filesystem::path const& path,
                          HttpClient& http_client,
                          cppx::http::transfer::TransferOptions options = {})
    -> std::expected<
        cppx::http::transfer::TransferResult,
        cppx::http::transfer::TransferError> {
    using backend_t = cppx::http::transfer::TransferBackend;
    using error_code_t = cppx::http::transfer::transfer_error_code;

    switch (options.backend) {
    case backend_t::CppxHttp:
        return cppx::http::transfer::detail::cppx_backend::download_file(
            http_client,
            options,
            url,
            path);
    case backend_t::Shell:
        return cppx::http::transfer::detail::shell_backend::download_file(
            options,
            url,
            path);
    case backend_t::Auto:
        return cppx::http::transfer::detail::with_shell_fallback(
            "download",
            cppx::http::transfer::detail::cppx_backend::download_file(
                http_client,
                options,
                url,
                path),
            [&] {
                return cppx::http::transfer::detail::shell_backend::download_file(
                    options,
                    url,
                    path);
            });
    }

    return cppx::http::transfer::detail::make_error(
        error_code_t::unsupported,
        backend_t::Auto,
        "unsupported transfer backend");
}

inline auto download_file(std::string_view url,
                          std::filesystem::path const& path,
                          cppx::http::transfer::TransferOptions options = {})
    -> std::expected<
        cppx::http::transfer::TransferResult,
        cppx::http::transfer::TransferError> {
    return cppx::http::transfer::detail::dispatch_download_file(
        url,
        path,
        options);
}

} // namespace cppx::http::transfer::system
