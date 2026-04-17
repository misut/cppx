export module cppx.checksum.system;
import std;
import cppx.checksum;
import cppx.env.system;
import cppx.process;
import cppx.process.system;

namespace cppx::checksum::detail {

inline auto make_error(cppx::checksum::checksum_error_code code, std::string message)
    -> std::unexpected<cppx::checksum::checksum_error> {
    return std::unexpected(cppx::checksum::checksum_error{
        .code = code,
        .message = std::move(message),
    });
}

inline auto trim_line_endings(std::string_view text) -> std::string {
    auto trimmed = std::string{text};
    while (!trimmed.empty() &&
           (trimmed.back() == '\r' || trimmed.back() == '\n')) {
        trimmed.pop_back();
    }
    return trimmed;
}

inline auto parse_certutil_output(std::string_view text) -> std::optional<std::string> {
    auto cursor = std::size_t{0};
    while (cursor <= text.size()) {
        auto next = text.find('\n', cursor);
        auto line = text.substr(
            cursor,
            next == std::string_view::npos ? text.size() - cursor : next - cursor);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.remove_suffix(1);

        auto condensed = std::string{};
        for (auto ch : line) {
            if (ch != ' ' && ch != '\t')
                condensed.push_back(ch);
        }
        if (auto normalized = cppx::checksum::normalize_sha256(condensed))
            return normalized;

        if (next == std::string_view::npos)
            break;
        cursor = next + 1;
    }
    return std::nullopt;
}

inline auto parse_shasum_output(std::string_view text) -> std::optional<std::string> {
    auto trimmed = trim_line_endings(text);
    auto split = trimmed.find_first_of(" \t");
    auto digest = std::string_view{trimmed}.substr(0, split);
    return cppx::checksum::normalize_sha256(digest);
}

} // namespace cppx::checksum::detail

export namespace cppx::checksum::system {

inline auto sha256_file(std::filesystem::path const& path)
    -> std::expected<std::string, cppx::checksum::checksum_error> {
#if defined(_WIN32)
    auto tool = cppx::env::system::find_in_path("certutil");
    if (!tool) {
        return cppx::checksum::detail::make_error(
            cppx::checksum::checksum_error_code::tool_not_found,
            "certutil is required but was not found");
    }

    auto result = cppx::process::system::capture({
        .program = tool->string(),
        .args = {"-hashfile", path.string(), "SHA256"},
    });
    if (!result) {
        return cppx::checksum::detail::make_error(
            cppx::checksum::checksum_error_code::hash_failed,
            std::format(
                "certutil failed to start: {}",
                cppx::process::to_string(result.error())));
    }
    if (result->timed_out || result->exit_code != 0) {
        auto stderr_text = cppx::checksum::detail::trim_line_endings(result->stderr_text);
        return cppx::checksum::detail::make_error(
            cppx::checksum::checksum_error_code::hash_failed,
            stderr_text.empty()
                ? std::format("certutil failed with exit code {}", result->exit_code)
                : stderr_text);
    }

    if (auto digest = cppx::checksum::detail::parse_certutil_output(result->stdout_text))
        return *digest;
    return cppx::checksum::detail::make_error(
        cppx::checksum::checksum_error_code::parse_failed,
        "could not parse certutil SHA-256 output");
#elif defined(__APPLE__) || defined(__linux__)
    auto tool = cppx::env::system::find_in_path("shasum");
    if (!tool) {
        return cppx::checksum::detail::make_error(
            cppx::checksum::checksum_error_code::tool_not_found,
            "shasum is required but was not found");
    }

    auto result = cppx::process::system::capture({
        .program = tool->string(),
        .args = {"-a", "256", path.string()},
    });
    if (!result) {
        return cppx::checksum::detail::make_error(
            cppx::checksum::checksum_error_code::hash_failed,
            std::format(
                "shasum failed to start: {}",
                cppx::process::to_string(result.error())));
    }
    if (result->timed_out || result->exit_code != 0) {
        auto stderr_text = cppx::checksum::detail::trim_line_endings(result->stderr_text);
        return cppx::checksum::detail::make_error(
            cppx::checksum::checksum_error_code::hash_failed,
            stderr_text.empty()
                ? std::format("shasum failed with exit code {}", result->exit_code)
                : stderr_text);
    }

    if (auto digest = cppx::checksum::detail::parse_shasum_output(result->stdout_text))
        return *digest;
    return cppx::checksum::detail::make_error(
        cppx::checksum::checksum_error_code::parse_failed,
        "could not parse shasum SHA-256 output");
#else
    return cppx::checksum::detail::make_error(
        cppx::checksum::checksum_error_code::unsupported,
        "SHA-256 hashing is unsupported on this platform");
#endif
}

} // namespace cppx::checksum::system
