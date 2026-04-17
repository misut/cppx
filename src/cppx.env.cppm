// Pure environment helpers. Side effects (std::getenv,
// std::filesystem) are pushed out to cppx.env.system. Everything in
// this module is a pure function over an injected capability so
// tests can substitute fakes and consumers can compose without
// touching real OS state.

export module cppx.env;
import std;

namespace cppx::env::detail {

inline auto trim_ascii(std::string_view value) -> std::string_view {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);
    return value;
}

inline auto ascii_lower(std::string_view value) -> std::string {
    auto lowered = std::string{};
    lowered.reserve(value.size());
    for (auto ch : value)
        lowered.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    return lowered;
}

} // namespace cppx::env::detail

export namespace cppx::env {

// ---- constants -----------------------------------------------------------

#if defined(_WIN32)
inline constexpr char PATH_SEPARATOR = ';';
inline constexpr std::string_view EXE_SUFFIX = ".exe";
#else
inline constexpr char PATH_SEPARATOR = ':';
inline constexpr std::string_view EXE_SUFFIX = "";
#endif

// ---- capability concepts -------------------------------------------------

// Anything that can answer "what's the value of $NAME?". Returns
// nullopt for unset OR empty (callers treat both as "missing").
template <class T>
concept env_source = requires(T const& t, std::string_view name) {
    { t.get(name) } -> std::same_as<std::optional<std::string>>;
};

// Anything that can answer "is this path a regular file?".
template <class T>
concept fs_source = requires(T const& t, std::filesystem::path const& p) {
    { t.is_regular_file(p) } -> std::same_as<bool>;
};

// ---- errors --------------------------------------------------------------

enum class find_error {
    no_PATH_set,        // $PATH itself is unset
    not_found_on_PATH,  // $PATH is set but `name` not found
};

enum class bool_parse_error {
    invalid_value,
};

// ---- pure functions ------------------------------------------------------

// Symmetric one-line forwarder so callers can route every env read
// through cppx::env::get and only swap the capability at the edge.
template <env_source E>
std::optional<std::string> get(E const& env, std::string_view name) {
    return env.get(name);
}

inline std::expected<bool, bool_parse_error> parse_bool(std::string_view value) {
    auto const normalized = detail::ascii_lower(detail::trim_ascii(value));
    if (normalized == "1" || normalized == "true" || normalized == "yes"
        || normalized == "on" || normalized == "y" || normalized == "t")
        return true;
    if (normalized == "0" || normalized == "false" || normalized == "no"
        || normalized == "off" || normalized == "n" || normalized == "f")
        return false;
    return std::unexpected{bool_parse_error::invalid_value};
}

template <env_source E>
std::optional<bool> get_bool(E const& env, std::string_view name) {
    auto value = get(env, name);
    if (!value)
        return std::nullopt;
    auto parsed = parse_bool(*value);
    if (!parsed)
        return std::nullopt;
    return *parsed;
}

template <env_source E>
bool get_bool_or(E const& env, std::string_view name, bool default_value) {
    auto value = get_bool(env, name);
    return value.value_or(default_value);
}

// Returns the user's home directory. Checks $HOME first, falls back
// to %USERPROFILE% on Windows. Pure with respect to `env`.
template <env_source E>
std::optional<std::filesystem::path> home_dir(E const& env) {
    if (auto h = env.get("HOME"))
        return std::filesystem::path{*h};
#if defined(_WIN32)
    if (auto up = env.get("USERPROFILE"))
        return std::filesystem::path{*up};
#endif
    return std::nullopt;
}

// Searches PATH for an executable named `name`. Returns the first
// match. Distinguishes "PATH unset" from "name not found" so
// consumers can give better error messages.
template <env_source E, fs_source F>
std::expected<std::filesystem::path, find_error>
find_in_path(E const& env, F const& fs, std::string_view name) {
    auto path_env = env.get("PATH");
    if (!path_env)
        return std::unexpected{find_error::no_PATH_set};

    // On Windows, also try `<name>.exe` if `name` has no extension.
    auto candidates = std::vector<std::string>{std::string{name}};
#if defined(_WIN32)
    if (!std::filesystem::path{name}.has_extension())
        candidates.push_back(std::string{name} + std::string{EXE_SUFFIX});
#endif

    namespace rv = std::ranges::views;
    auto const sep = std::string_view{&PATH_SEPARATOR, 1};

    for (auto dir_chars : *path_env | rv::split(sep)) {
        auto const dir_sv = std::string_view{
            std::ranges::begin(dir_chars), std::ranges::end(dir_chars)};
        if (dir_sv.empty()) continue;
        auto const dir = std::filesystem::path{dir_sv};
        for (auto const& cand : candidates) {
            auto full = dir / cand;
            if (fs.is_regular_file(full))
                return full;
        }
    }
    return std::unexpected{find_error::not_found_on_PATH};
}

// ---- pure helper ---------------------------------------------------------

// Wraps `s` in double quotes if it contains whitespace, otherwise
// returns it unchanged. NOT a general shell escape — just a
// whitespace-safe path wrapper for std::system() command building.
inline std::string shell_quote(std::string_view s) {
    if (s.find_first_of(" \t") == std::string_view::npos)
        return std::string{s};
    return std::format("\"{}\"", s);
}

// ---- test doubles --------------------------------------------------------

// Empty capabilities that always answer "missing". Exported so
// downstream consumers can compose them into their own fakes.
struct null_env {
    std::optional<std::string> get(std::string_view) const {
        return std::nullopt;
    }
};
struct null_fs {
    bool is_regular_file(std::filesystem::path const&) const {
        return false;
    }
};

} // namespace cppx::env
