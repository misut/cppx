// Host OS / architecture detection, resolved at compile time.
// Zero runtime cost — host() is consteval, so the result is baked in.

export module cppx.platform;
import std;

export namespace cppx::platform {

namespace detail {

constexpr auto ascii_lower(char ch) noexcept -> char {
    return (ch >= 'A' && ch <= 'Z')
        ? static_cast<char>(ch - 'A' + 'a')
        : ch;
}

inline auto normalize_token(std::string_view value) -> std::string {
    auto out = std::string{};
    out.reserve(value.size());
    for (char ch : value) {
        if (!std::isspace(static_cast<unsigned char>(ch)))
            out.push_back(ascii_lower(ch));
    }
    return out;
}

} // namespace detail

enum class OS {
    Unknown,
    Linux,
    MacOS,
    Windows,
    WASI,
    Android,
};

enum class Arch {
    Unknown,
    X86_64,
    AArch64,
    Wasm32,
};

constexpr std::string_view os_name(OS os) noexcept {
    switch (os) {
        case OS::Linux:   return "linux";
        case OS::MacOS:   return "macos";
        case OS::Windows: return "windows";
        case OS::WASI:    return "wasi";
        case OS::Android: return "android";
        case OS::Unknown: return "unknown";
    }
    return "unknown";
}

constexpr std::string_view arch_name(Arch arch) noexcept {
    switch (arch) {
        case Arch::X86_64:  return "x86_64";
        case Arch::AArch64: return "aarch64";
        case Arch::Wasm32:  return "wasm32";
        case Arch::Unknown: return "unknown";
    }
    return "unknown";
}

struct Platform {
    OS os;
    Arch arch;

    // Wildcard semantics: an Unknown field on either side acts as "any".
    constexpr bool matches(Platform const& other) const noexcept {
        if (os != OS::Unknown && other.os != OS::Unknown && os != other.os)
            return false;
        if (arch != Arch::Unknown && other.arch != Arch::Unknown && arch != other.arch)
            return false;
        return true;
    }

    // Canonical short form: "os-arch" (e.g. "linux-x86_64", "macos-aarch64").
    // When either field is Unknown, the known half is returned alone;
    // fully unknown returns "any".
    std::string to_string() const {
        bool const has_os = os != OS::Unknown;
        bool const has_arch = arch != Arch::Unknown;
        if (has_os && has_arch)
            return std::format("{}-{}", os_name(os), arch_name(arch));
        if (has_os)
            return std::string{os_name(os)};
        if (has_arch)
            return std::string{arch_name(arch)};
        return "any";
    }
};

consteval Platform host() noexcept {
    Platform p{OS::Unknown, Arch::Unknown};

#if defined(__APPLE__)
    p.os = OS::MacOS;
#elif defined(__ANDROID__)
    p.os = OS::Android;
#elif defined(__linux__)
    p.os = OS::Linux;
#elif defined(_WIN32)
    p.os = OS::Windows;
#elif defined(__wasi__)
    p.os = OS::WASI;
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
    p.arch = Arch::AArch64;
#elif defined(__x86_64__) || defined(_M_X64)
    p.arch = Arch::X86_64;
#elif defined(__wasm32__)
    p.arch = Arch::Wasm32;
#endif

    return p;
}

inline auto parse_os(std::string_view value) -> OS {
    auto const token = detail::normalize_token(value);
    if (token == "linux")
        return OS::Linux;
    if (token == "macos")
        return OS::MacOS;
    if (token == "windows")
        return OS::Windows;
    if (token == "wasi")
        return OS::WASI;
    if (token == "android")
        return OS::Android;
    return OS::Unknown;
}

inline auto parse_arch(std::string_view value) -> Arch {
    auto const token = detail::normalize_token(value);
    if (token == "x86_64")
        return Arch::X86_64;
    if (token == "aarch64")
        return Arch::AArch64;
    if (token == "wasm32")
        return Arch::Wasm32;
    return Arch::Unknown;
}

inline auto parse_platform(std::string_view value) -> Platform {
    auto const token = detail::normalize_token(value);
    if (token.empty() || token == "any")
        return Platform{OS::Unknown, Arch::Unknown};

    if (auto os = parse_os(token); os != OS::Unknown)
        return Platform{os, Arch::Unknown};
    if (auto arch = parse_arch(token); arch != Arch::Unknown)
        return Platform{OS::Unknown, arch};

    auto const dash = token.find('-');
    if (dash == std::string::npos)
        return Platform{OS::Unknown, Arch::Unknown};

    auto const left = std::string_view{token}.substr(0, dash);
    auto const right = std::string_view{token}.substr(dash + 1);

    auto const os_left = parse_os(left);
    auto const arch_right = parse_arch(right);
    if (os_left != OS::Unknown && arch_right != Arch::Unknown) {
        return Platform{os_left, arch_right};
    }

    auto const arch_left = parse_arch(left);
    auto const os_right = parse_os(right);
    if (os_right != OS::Unknown && arch_left != Arch::Unknown) {
        return Platform{os_right, arch_left};
    }

    // 3-part Android triples: "<arch>-linux-android" and "<arch>-linux-android<N>"
    // (e.g. aarch64-linux-android, aarch64-linux-android33).
    if (auto pos = token.find("-linux-android"); pos != std::string_view::npos) {
        auto const arch_part = std::string_view{token}.substr(0, pos);
        if (auto arch = parse_arch(arch_part); arch != Arch::Unknown) {
            return Platform{OS::Android, arch};
        }
    }

    return Platform{OS::Unknown, Arch::Unknown};
}

inline auto platform_from_target_triple(std::string_view value)
    -> std::optional<Platform> {
    auto const platform = parse_platform(value);
    if (platform.os == OS::Unknown || platform.arch == Arch::Unknown)
        return std::nullopt;
    return platform;
}

} // namespace cppx::platform
