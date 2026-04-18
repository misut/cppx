// System-backed URL opening. This is the impure edge for cppx.os.

module;

#if defined(__APPLE__) || defined(__linux__)
#include <cerrno>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#endif

export module cppx.os.system;
import std;
import cppx.env.system;
import cppx.os;
import cppx.resource;
import cppx.unicode;

namespace cppx::os::detail {

#if defined(__APPLE__) || defined(__linux__)
inline std::expected<void, cppx::os::open_error> spawn_and_wait(
        std::string const& launcher,
        std::string_view target) {
    auto const target_str = std::string{target};
    auto launcher_arg = const_cast<char*>(launcher.c_str());
    auto target_arg = const_cast<char*>(target_str.c_str());
    char* const argv[] = {launcher_arg, target_arg, nullptr};

    pid_t pid = 0;
    auto rc = ::posix_spawn(&pid, launcher.c_str(), nullptr, nullptr, argv, environ);
    if (rc != 0) {
        if (rc == ENOENT)
            return std::unexpected{cppx::os::open_error::backend_unavailable};
        return std::unexpected{cppx::os::open_error::open_failed};
    }

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0)
        return std::unexpected{cppx::os::open_error::open_failed};

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return {};
    return std::unexpected{cppx::os::open_error::open_failed};
}
#endif

} // namespace cppx::os::detail

export namespace cppx::os::system {

auto open_url(std::string_view url) -> std::expected<void, cppx::os::open_error>;

} // namespace cppx::os::system

module :private;

namespace cppx::os::system {

auto open_url(std::string_view url) -> std::expected<void, cppx::os::open_error> {
    auto const kind = cppx::resource::classify(url);
    if (!cppx::resource::is_url(kind))
        return std::unexpected{cppx::os::open_error::invalid_target};

#if defined(_WIN32)
    auto wide = cppx::unicode::utf8_to_wide(url);
    if (!wide || wide->empty())
        return std::unexpected{cppx::os::open_error::invalid_target};

    auto const result = reinterpret_cast<std::intptr_t>(
        ::ShellExecuteW(nullptr, L"open", wide->c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32)
        return std::unexpected{cppx::os::open_error::open_failed};
    return {};
#elif defined(__APPLE__)
    auto const launcher = std::filesystem::path{"/usr/bin/open"};
    std::error_code ec;
    if (!std::filesystem::is_regular_file(launcher, ec))
        return std::unexpected{cppx::os::open_error::backend_unavailable};
    return cppx::os::detail::spawn_and_wait(launcher.string(), url);
#elif defined(__linux__)
    auto launcher = cppx::env::system::find_in_path("xdg-open");
    if (!launcher)
        return std::unexpected{cppx::os::open_error::backend_unavailable};
    return cppx::os::detail::spawn_and_wait(launcher->string(), url);
#else
    (void)url;
    return std::unexpected{cppx::os::open_error::unsupported};
#endif
}

} // namespace cppx::os::system
