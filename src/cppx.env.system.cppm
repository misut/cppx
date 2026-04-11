// The only impure submodule in cppx. Wraps std::getenv and
// std::filesystem so cppx.env can stay pure. Importing this module
// is the audit signal for "this translation unit reads real OS
// state" — grep for `import cppx.env.system` to find every
// side-effecting call site.

export module cppx.env.system;
import std;
import cppx.env;

export namespace cppx::env::system {

// Reads environment variables via std::getenv. NOTE: std::getenv is
// not safe to call concurrently with setenv/putenv. cppx callers
// invoke this from single-threaded startup code, where that
// restriction is harmless. Returns nullopt for unset OR empty.
struct system_env {
    std::optional<std::string> get(std::string_view name) const {
        auto const buf = std::string{name};
        if (auto const* v = std::getenv(buf.c_str()); v && *v)
            return std::string{v};
        return std::nullopt;
    }
};

// Wraps std::filesystem::is_regular_file with an error_code so
// permission errors / broken symlinks fall through as "not a file"
// instead of throwing.
struct system_fs {
    bool is_regular_file(std::filesystem::path const& p) const {
        std::error_code ec;
        return std::filesystem::is_regular_file(p, ec);
    }
};

// Convenience forwarders bound to the system capabilities so call
// sites don't have to construct empty structs every time. Each one
// is a one-line wrapper around the pure templated version.

inline std::optional<std::string> get(std::string_view name) {
    return cppx::env::get(system_env{}, name);
}

inline std::optional<std::filesystem::path> home_dir() {
    return cppx::env::home_dir(system_env{});
}

inline std::expected<std::filesystem::path, cppx::env::find_error>
find_in_path(std::string_view name) {
    return cppx::env::find_in_path(system_env{}, system_fs{}, name);
}

} // namespace cppx::env::system
