// Environment and shell helpers: cross-platform constants, $HOME lookup,
// PATH search, and shell quoting. Consolidates shims previously duplicated
// across misut/exon and misut/intron.

export module cppx.env;
import std;

export namespace cppx::env {

// Platform-independent constants.
#if defined(_WIN32)
inline constexpr char PATH_SEPARATOR = ';';
inline constexpr std::string_view EXE_SUFFIX = ".exe";
#else
inline constexpr char PATH_SEPARATOR = ':';
inline constexpr std::string_view EXE_SUFFIX = "";
#endif

// Returns the named environment variable. Returns std::nullopt when the
// variable is unset OR set to an empty string — callers typically treat
// both as "not provided".
inline std::optional<std::string> get(std::string_view name) {
    // std::getenv takes a null-terminated C string.
    auto buf = std::string{name};
    if (auto const* v = std::getenv(buf.c_str()); v && *v)
        return std::string{v};
    return std::nullopt;
}

// Returns the user's home directory. Checks $HOME first, falls back to
// %USERPROFILE% on Windows. Returns std::nullopt if neither is set —
// callers decide whether to throw, default, or propagate.
inline std::optional<std::filesystem::path> home_dir() {
    if (auto h = get("HOME"))
        return std::filesystem::path{*h};
#if defined(_WIN32)
    if (auto up = get("USERPROFILE"))
        return std::filesystem::path{*up};
#endif
    return std::nullopt;
}

// Searches PATH for an executable named `name`. On Windows, appends
// EXE_SUFFIX if the name has no extension. Returns the first match, or
// std::nullopt if not found. Matches must be regular files (not dirs).
inline std::optional<std::filesystem::path> find_in_path(std::string_view name) {
    auto path_env = get("PATH");
    if (!path_env)
        return std::nullopt;

    // On Windows, also try `<name>.exe` if `name` has no extension.
    std::string with_suffix;
    std::string_view candidates[2] = {name, {}};
    std::size_t candidate_count = 1;
#if defined(_WIN32)
    auto stem = std::filesystem::path{name};
    if (!stem.has_extension()) {
        with_suffix = std::string{name} + std::string{EXE_SUFFIX};
        candidates[1] = with_suffix;
        candidate_count = 2;
    }
#endif

    auto const& dirs = *path_env;
    std::size_t pos = 0;
    while (pos <= dirs.size()) {
        auto sep = dirs.find(PATH_SEPARATOR, pos);
        auto end = (sep == std::string::npos) ? dirs.size() : sep;
        if (end > pos) {
            auto dir = std::filesystem::path{dirs.substr(pos, end - pos)};
            for (std::size_t i = 0; i < candidate_count; ++i) {
                auto full = dir / std::string{candidates[i]};
                std::error_code ec;
                if (std::filesystem::is_regular_file(full, ec))
                    return full;
            }
        }
        if (sep == std::string::npos) break;
        pos = sep + 1;
    }
    return std::nullopt;
}

// Wraps `s` in double quotes if it contains whitespace, otherwise returns
// it unchanged. Suitable for piecewise std::system() command construction.
// Does NOT escape inner quotes or backslashes — this is not a general
// shell-escape routine, just a whitespace-safe path wrapper.
inline std::string shell_quote(std::string_view s) {
    if (s.find_first_of(" \t") == std::string_view::npos)
        return std::string{s};
    return std::format("\"{}\"", s);
}

} // namespace cppx::env
