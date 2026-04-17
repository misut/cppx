// OS-facing capability declarations. Side effects stay in cppx.os.system.

export module cppx.os;
import std;

export namespace cppx::os {

enum class open_error {
    invalid_target,
    unsupported,
    backend_unavailable,
    open_failed,
};

inline constexpr auto to_string(open_error error) -> std::string_view {
    switch (error) {
    case open_error::invalid_target:
        return "invalid_target";
    case open_error::unsupported:
        return "unsupported";
    case open_error::backend_unavailable:
        return "backend_unavailable";
    case open_error::open_failed:
        return "open_failed";
    }
    return "open_failed";
}

} // namespace cppx::os
