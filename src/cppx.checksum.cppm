export module cppx.checksum;
import std;

export namespace cppx::checksum {

enum class checksum_error_code {
    tool_not_found,
    hash_failed,
    parse_failed,
    unsupported,
};

inline constexpr auto to_string(checksum_error_code code) -> std::string_view {
    switch (code) {
    case checksum_error_code::tool_not_found:
        return "tool_not_found";
    case checksum_error_code::hash_failed:
        return "hash_failed";
    case checksum_error_code::parse_failed:
        return "parse_failed";
    case checksum_error_code::unsupported:
        return "unsupported";
    }
    return "hash_failed";
}

struct checksum_error {
    checksum_error_code code = checksum_error_code::hash_failed;
    std::string message;
};

inline auto normalize_sha256(std::string_view digest) -> std::optional<std::string> {
    auto normalized = std::string{};
    normalized.reserve(digest.size());
    for (auto ch : digest) {
        auto lowered = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (!std::isxdigit(static_cast<unsigned char>(lowered)))
            return std::nullopt;
        normalized.push_back(lowered);
    }
    if (normalized.size() != 64)
        return std::nullopt;
    return normalized;
}

inline auto find_sha256_for_filename(std::string_view manifest,
                                     std::string_view filename)
    -> std::optional<std::string> {
    auto cursor = std::size_t{0};
    while (cursor <= manifest.size()) {
        auto next = manifest.find('\n', cursor);
        auto line = manifest.substr(
            cursor,
            next == std::string_view::npos ? manifest.size() - cursor : next - cursor);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.remove_suffix(1);

        auto split = line.find_first_of(" \t");
        if (split != std::string_view::npos) {
            auto digest = line.substr(0, split);
            auto rest = line.substr(split);
            while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
                rest.remove_prefix(1);
            if (!rest.empty() && rest.front() == '*')
                rest.remove_prefix(1);
            if (rest == filename) {
                if (auto normalized = normalize_sha256(digest))
                    return normalized;
            }
        }

        if (next == std::string_view::npos)
            break;
        cursor = next + 1;
    }
    return std::nullopt;
}

} // namespace cppx::checksum
