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

} // namespace cppx::resource::detail

export namespace cppx::resource {

enum class resource_kind {
    filesystem_path,
    http_url,
    https_url,
    other_url,
};

inline resource_kind classify(std::string_view value) {
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
