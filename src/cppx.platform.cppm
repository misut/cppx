// Host OS / architecture detection, resolved at compile time.
// Zero runtime cost — host() is consteval, so the result is baked in.

export module cppx.platform;
import std;

export namespace cppx::platform {

enum class OS {
    Unknown,
    Linux,
    MacOS,
    Windows,
    WASI,
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

} // namespace cppx::platform
