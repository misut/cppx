// Resource classification helpers. Keeps the "is this a path or a URL?"
// decision pure so higher-level modules can share one set of rules.

export module cppx.resource;
import std;

namespace cppx::resource::detail {

inline bool looks_like_windows_drive_path(std::string_view value) {
    if (value.size() < 2)
        return false;
    if (!std::isalpha(static_cast<unsigned char>(value[0])) || value[1] != ':')
        return false;
    return value.size() == 2 || value[2] == '/' || value[2] == '\\';
}

inline bool looks_like_scheme(std::string_view value) {
    if (value.empty()
        || !std::isalpha(static_cast<unsigned char>(value.front())))
        return false;
    auto colon = value.find(':');
    if (colon == std::string_view::npos)
        return false;
    for (std::size_t i = 1; i < colon; ++i) {
        auto ch = static_cast<unsigned char>(value[i]);
        if (!std::isalnum(ch) && ch != '+' && ch != '-' && ch != '.')
            return false;
    }
    return true;
}

inline auto ascii_lower(char ch) -> char {
    if (ch >= 'A' && ch <= 'Z')
        return static_cast<char>(ch - 'A' + 'a');
    return ch;
}

inline bool ascii_iequals(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size())
        return false;

    for (auto i = std::size_t{0}; i < lhs.size(); ++i) {
        if (ascii_lower(lhs[i]) != ascii_lower(rhs[i]))
            return false;
    }
    return true;
}

inline auto hex_value(char ch) -> std::optional<std::uint8_t> {
    if (ch >= '0' && ch <= '9')
        return static_cast<std::uint8_t>(ch - '0');
    if (ch >= 'a' && ch <= 'f')
        return static_cast<std::uint8_t>(ch - 'a' + 10);
    if (ch >= 'A' && ch <= 'F')
        return static_cast<std::uint8_t>(ch - 'A' + 10);
    return std::nullopt;
}

inline auto percent_decode(std::string_view value) -> std::optional<std::string> {
    auto decoded = std::string{};
    decoded.reserve(value.size());

    for (auto i = std::size_t{0}; i < value.size(); ++i) {
        if (value[i] != '%') {
            decoded.push_back(value[i]);
            continue;
        }

        if (i + 2 >= value.size())
            return std::nullopt;

        auto hi = hex_value(value[i + 1]);
        auto lo = hex_value(value[i + 2]);
        if (!hi || !lo)
            return std::nullopt;

        decoded.push_back(static_cast<char>((*hi << 4) | *lo));
        i += 2;
    }

    return decoded;
}

inline auto utf8_path(std::string_view value) -> std::filesystem::path {
    auto utf8 = std::u8string{};
    utf8.reserve(value.size());
    for (auto ch : value)
        utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(ch)));
    return std::filesystem::path{utf8};
}

inline auto normalize_file_url_path(std::string decoded)
    -> std::optional<std::filesystem::path> {
    if (decoded.empty() || !decoded.starts_with('/'))
        return std::nullopt;

    if (decoded.starts_with("//"))
        return std::nullopt;

#if defined(_WIN32)
    if (decoded.size() >= 4
        && decoded[0] == '/'
        && looks_like_windows_drive_path(decoded.substr(1))) {
        decoded.erase(0, 1);
    }
#endif

    return utf8_path(decoded).lexically_normal();
}

} // namespace cppx::resource::detail

export namespace cppx::resource {

enum class resource_kind {
    filesystem_path,
    file_url,
    http_url,
    https_url,
    other_url,
};

inline resource_kind classify(std::string_view value) {
    if (value.starts_with("file:"))
        return resource_kind::file_url;
    if (value.starts_with("http://"))
        return resource_kind::http_url;
    if (value.starts_with("https://"))
        return resource_kind::https_url;
    if (detail::looks_like_windows_drive_path(value))
        return resource_kind::filesystem_path;
    if (detail::looks_like_scheme(value))
        return resource_kind::other_url;
    return resource_kind::filesystem_path;
}

inline bool is_url(resource_kind kind) {
    return kind != resource_kind::filesystem_path;
}

inline bool is_url(std::string_view value) {
    return is_url(classify(value));
}

inline bool is_remote(resource_kind kind) {
    return kind == resource_kind::http_url
        || kind == resource_kind::https_url;
}

inline bool is_remote(std::string_view value) {
    return is_remote(classify(value));
}

inline auto resolve_file_url(std::string_view locator)
    -> std::optional<std::filesystem::path> {
    if (classify(locator) != resource_kind::file_url)
        return std::nullopt;

    auto remainder = locator.substr(std::string_view{"file:"}.size());
    auto authority = std::string_view{};
    auto path_part = remainder;

    if (remainder.starts_with("//")) {
        auto auth_and_path = remainder.substr(2);
        auto slash = auth_and_path.find('/');
        if (slash == std::string_view::npos) {
            authority = auth_and_path;
            path_part = {};
        } else {
            authority = auth_and_path.substr(0, slash);
            path_part = auth_and_path.substr(slash);
        }

        if (!authority.empty() && !detail::ascii_iequals(authority, "localhost"))
            return std::nullopt;
    }

    auto decoded = detail::percent_decode(path_part);
    if (!decoded)
        return std::nullopt;

    return detail::normalize_file_url_path(std::move(*decoded));
}

inline std::filesystem::path resolve_path(
        std::filesystem::path const& base,
        std::filesystem::path const& value) {
    if (value.is_absolute())
        return value.lexically_normal();
    return (base / value).lexically_normal();
}

inline std::filesystem::path resolve_path(
        std::filesystem::path const& base,
        std::string_view value) {
    if (detail::looks_like_windows_drive_path(value))
        return std::filesystem::path{std::string{value}}.lexically_normal();
    return resolve_path(base, std::filesystem::path{std::string{value}});
}

} // namespace cppx::resource
