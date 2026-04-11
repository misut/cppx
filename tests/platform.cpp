import cppx.platform;
import std;

int failed = 0;

void check(bool cond, std::string_view msg) {
    if (!cond) {
        std::println(std::cerr, "FAIL: {}", msg);
        ++failed;
    }
}

using cppx::platform::OS;
using cppx::platform::Arch;
using cppx::platform::Platform;

void test_host() {
    constexpr auto h = cppx::platform::host();
    // On supported CI platforms (macos-15, ubuntu-24.04{,-arm}, windows-2022)
    // both os and arch resolve to a known value.
    check(h.os != OS::Unknown, "host os is known");
    check(h.arch != Arch::Unknown, "host arch is known");
}

void test_os_name() {
    static_assert(cppx::platform::os_name(OS::Linux) == "linux");
    static_assert(cppx::platform::os_name(OS::MacOS) == "macos");
    static_assert(cppx::platform::os_name(OS::Windows) == "windows");
    static_assert(cppx::platform::os_name(OS::WASI) == "wasi");
    static_assert(cppx::platform::os_name(OS::Unknown) == "unknown");
    check(true, "os_name table");
}

void test_arch_name() {
    static_assert(cppx::platform::arch_name(Arch::X86_64) == "x86_64");
    static_assert(cppx::platform::arch_name(Arch::AArch64) == "aarch64");
    static_assert(cppx::platform::arch_name(Arch::Wasm32) == "wasm32");
    static_assert(cppx::platform::arch_name(Arch::Unknown) == "unknown");
    check(true, "arch_name table");
}

void test_to_string() {
    auto both = Platform{OS::Linux, Arch::X86_64}.to_string();
    check(both == "linux-x86_64", "full triple");

    auto os_only = Platform{OS::MacOS, Arch::Unknown}.to_string();
    check(os_only == "macos", "os-only");

    auto arch_only = Platform{OS::Unknown, Arch::AArch64}.to_string();
    check(arch_only == "aarch64", "arch-only");

    auto none = Platform{OS::Unknown, Arch::Unknown}.to_string();
    check(none == "any", "fully unknown is 'any'");
}

void test_matches() {
    constexpr auto host = cppx::platform::host();

    // Exact match.
    static_assert(host.matches(host));

    // Wildcard: Unknown on either side matches any value.
    constexpr auto any_os = Platform{OS::Unknown, host.arch};
    check(host.matches(any_os), "Unknown os acts as wildcard");

    constexpr auto any_arch = Platform{host.os, Arch::Unknown};
    check(host.matches(any_arch), "Unknown arch acts as wildcard");

    constexpr auto fully_any = Platform{OS::Unknown, Arch::Unknown};
    check(host.matches(fully_any), "fully-unknown matches anything");

    // Non-matches: different known values.
    constexpr auto different = Platform{
        (host.os == OS::Linux) ? OS::MacOS : OS::Linux,
        host.arch,
    };
    check(!host.matches(different), "different os does not match");
}

int main() {
    test_host();
    test_os_name();
    test_arch_name();
    test_to_string();
    test_matches();

    if (failed > 0) {
        std::println(std::cerr, "\n{} test(s) failed", failed);
        return 1;
    }
    std::println("all cppx.platform tests passed");
    return 0;
}
