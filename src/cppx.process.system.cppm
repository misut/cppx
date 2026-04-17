module;

#if defined(__APPLE__) || defined(__linux__)
#include <cstdlib>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

export module cppx.process.system;
import std;
import cppx.process;

#if defined(_WIN32)
import cppx.unicode;
#endif

namespace cppx::process::detail {

inline auto normalize_exit_status(int status) noexcept -> int {
    if (status == -1)
        return 1;
    if ((status & 0x7f) == 0)
        return (status >> 8) & 0xff;
    if ((status & 0x7f) != 0x7f)
        return 128 + (status & 0x7f);
    return status;
}

#if defined(_WIN32)

inline auto quote_windows_arg(std::wstring_view value) -> std::wstring {
    if (value.empty())
        return L"\"\"";

    bool needs_quotes = false;
    for (auto ch : value) {
        if (ch == L' ' || ch == L'\t' || ch == L'"') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes)
        return std::wstring{value};

    auto out = std::wstring{};
    out.push_back(L'"');

    std::size_t backslashes = 0;
    for (auto ch : value) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
            backslashes = 0;
            continue;
        }
        if (backslashes > 0) {
            out.append(backslashes, L'\\');
            backslashes = 0;
        }
        out.push_back(ch);
    }

    if (backslashes > 0)
        out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

inline auto utf8_to_wide_checked(std::string_view value)
    -> std::expected<std::wstring, cppx::process::process_error> {
    auto wide = cppx::unicode::utf8_to_wide(value);
    if (!wide)
        return std::unexpected{cppx::process::process_error::encoding_failed};
    return *wide;
}

inline auto build_command_line(cppx::process::ProcessSpec const& spec)
    -> std::expected<std::wstring, cppx::process::process_error> {
    auto program = utf8_to_wide_checked(spec.program);
    if (!program)
        return std::unexpected{program.error()};

    auto command_line = quote_windows_arg(*program);
    for (auto const& arg : spec.args) {
        auto wide_arg = utf8_to_wide_checked(arg);
        if (!wide_arg)
            return std::unexpected{wide_arg.error()};
        command_line.push_back(L' ');
        command_line += quote_windows_arg(*wide_arg);
    }
    return command_line;
}

inline auto build_environment_block(cppx::process::ProcessSpec const& spec)
    -> std::expected<std::vector<wchar_t>, cppx::process::process_error> {
    if (spec.env_overrides.empty())
        return std::vector<wchar_t>{};

    auto vars = std::vector<std::wstring>{};
    auto env_block = GetEnvironmentStringsW();
    if (env_block != nullptr) {
        for (auto* cursor = env_block; *cursor != L'\0'; cursor += std::wcslen(cursor) + 1)
            vars.emplace_back(cursor);
        FreeEnvironmentStringsW(env_block);
    }

    for (auto const& [name, value] : spec.env_overrides) {
        auto wide_name = utf8_to_wide_checked(name);
        auto wide_value = utf8_to_wide_checked(value);
        if (!wide_name || !wide_value)
            return std::unexpected{cppx::process::process_error::encoding_failed};

        auto prefix = *wide_name + L"=";
        std::erase_if(vars, [&](std::wstring const& entry) {
            return entry.starts_with(prefix);
        });
        vars.push_back(prefix + *wide_value);
    }

    auto block = std::vector<wchar_t>{};
    for (auto const& entry : vars) {
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

inline auto run_process(cppx::process::ProcessSpec const& spec)
    -> std::expected<cppx::process::ProcessResult, cppx::process::process_error> {
    auto command_line = build_command_line(spec);
    if (!command_line)
        return std::unexpected{command_line.error()};

    std::vector<wchar_t> mutable_command(command_line->begin(), command_line->end());
    mutable_command.push_back(L'\0');

    auto cwd = std::optional<std::wstring>{};
    if (!spec.cwd.empty()) {
        auto wide_cwd = cppx::unicode::utf8_to_wide(spec.cwd.string());
        if (!wide_cwd)
            return std::unexpected{cppx::process::process_error::encoding_failed};
        cwd = std::move(*wide_cwd);
    }

    auto environment = build_environment_block(spec);
    if (!environment)
        return std::unexpected{environment.error()};

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info,
                                     sizeof(info))) {
            CloseHandle(job);
            job = nullptr;
        }
    }

    auto old_error_mode =
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    auto restore_error_mode = [&] { SetErrorMode(old_error_mode); };

    auto const creation_flags =
        environment->empty() ? 0U : static_cast<unsigned>(CREATE_UNICODE_ENVIRONMENT);

    if (!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
                        creation_flags, environment->empty() ? nullptr : environment->data(),
                        cwd ? cwd->c_str() : nullptr, &si, &pi)) {
        auto error = GetLastError();
        if (job != nullptr)
            CloseHandle(job);
        restore_error_mode();
        if (error == ERROR_DIRECTORY)
            return std::unexpected{cppx::process::process_error::cwd_unavailable};
        return std::unexpected{cppx::process::process_error::spawn_failed};
    }

    if (job != nullptr && !AssignProcessToJobObject(job, pi.hProcess)) {
        CloseHandle(job);
        job = nullptr;
    }

    auto wait_ms = spec.timeout
        ? static_cast<DWORD>(std::min<std::chrono::milliseconds::rep>(
              spec.timeout->count(), std::numeric_limits<DWORD>::max()))
        : INFINITE;
    auto wait_result = WaitForSingleObject(pi.hProcess, wait_ms);

    cppx::process::ProcessResult result;
    if (wait_result == WAIT_TIMEOUT) {
        result.timed_out = true;
        if (job != nullptr)
            TerminateJobObject(job, 124);
        else
            TerminateProcess(pi.hProcess, 124);
        WaitForSingleObject(pi.hProcess, INFINITE);
        result.exit_code = 124;
    } else if (wait_result != WAIT_OBJECT_0) {
        if (job != nullptr)
            CloseHandle(job);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        restore_error_mode();
        return std::unexpected{cppx::process::process_error::wait_failed};
    } else {
        DWORD exit_code = 0;
        if (!GetExitCodeProcess(pi.hProcess, &exit_code)) {
            if (job != nullptr)
                CloseHandle(job);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            restore_error_mode();
            return std::unexpected{cppx::process::process_error::wait_failed};
        }
        result.exit_code = static_cast<int>(exit_code);
    }

    if (job != nullptr)
        CloseHandle(job);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    restore_error_mode();
    return result;
}

#elif defined(__APPLE__) || defined(__linux__)

struct child_failure {
    char stage = 'x';
    int error = 0;
};

inline void set_cloexec(int fd) {
    auto flags = ::fcntl(fd, F_GETFD);
    if (flags >= 0)
        ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

inline auto wait_for_child(pid_t pid,
                           std::optional<std::chrono::milliseconds> timeout)
    -> std::expected<cppx::process::ProcessResult, cppx::process::process_error> {
    auto deadline = timeout
        ? std::optional{std::chrono::steady_clock::now() + *timeout}
        : std::nullopt;

    int status = 0;
    while (true) {
        auto rc = ::waitpid(pid, &status, WNOHANG);
        if (rc == pid) {
            cppx::process::ProcessResult result;
            result.exit_code = normalize_exit_status(status);
            return result;
        }
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            return std::unexpected{cppx::process::process_error::wait_failed};
        }
        if (deadline && std::chrono::steady_clock::now() >= *deadline) {
            ::kill(pid, SIGKILL);
            while (::waitpid(pid, &status, 0) < 0) {
                if (errno != EINTR)
                    return std::unexpected{cppx::process::process_error::wait_failed};
            }
            return cppx::process::ProcessResult{.exit_code = 124, .timed_out = true};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
}

inline auto run_process(cppx::process::ProcessSpec const& spec)
    -> std::expected<cppx::process::ProcessResult, cppx::process::process_error> {
    std::array<int, 2> error_pipe{-1, -1};
    if (::pipe(error_pipe.data()) != 0)
        return std::unexpected{cppx::process::process_error::spawn_failed};
    set_cloexec(error_pipe[1]);

    auto argv_storage = std::vector<std::string>{spec.program};
    argv_storage.insert(argv_storage.end(), spec.args.begin(), spec.args.end());
    auto argv = std::vector<char*>{};
    argv.reserve(argv_storage.size() + 1);
    for (auto& arg : argv_storage)
        argv.push_back(arg.data());
    argv.push_back(nullptr);

    auto pid = ::fork();
    if (pid < 0) {
        ::close(error_pipe[0]);
        ::close(error_pipe[1]);
        return std::unexpected{cppx::process::process_error::spawn_failed};
    }

    if (pid == 0) {
        ::close(error_pipe[0]);

        if (!spec.cwd.empty() && ::chdir(spec.cwd.c_str()) != 0) {
            auto failure = child_failure{.stage = 'c', .error = errno};
            [[maybe_unused]] auto _ =
                ::write(error_pipe[1], &failure, sizeof(failure));
            _exit(127);
        }

        for (auto const& [name, value] : spec.env_overrides) {
            if (::setenv(name.c_str(), value.c_str(), 1) != 0) {
                auto failure = child_failure{.stage = 'e', .error = errno};
                [[maybe_unused]] auto _ =
                    ::write(error_pipe[1], &failure, sizeof(failure));
                _exit(127);
            }
        }

        ::execvp(argv[0], argv.data());
        auto failure = child_failure{.stage = 'x', .error = errno};
        [[maybe_unused]] auto _ =
            ::write(error_pipe[1], &failure, sizeof(failure));
        _exit(127);
    }

    ::close(error_pipe[1]);
    auto result = wait_for_child(pid, spec.timeout);

    child_failure failure{};
    auto const failure_size = ::read(error_pipe[0], &failure, sizeof(failure));
    ::close(error_pipe[0]);

    if (failure_size == static_cast<ssize_t>(sizeof(failure))) {
        switch (failure.stage) {
        case 'c':
            return std::unexpected{cppx::process::process_error::cwd_unavailable};
        case 'e':
            return std::unexpected{cppx::process::process_error::environment_failed};
        default:
            return std::unexpected{cppx::process::process_error::spawn_failed};
        }
    }

    return result;
}

#else

inline auto run_process(cppx::process::ProcessSpec const&)
    -> std::expected<cppx::process::ProcessResult, cppx::process::process_error> {
    return std::unexpected{cppx::process::process_error::unsupported};
}

#endif

} // namespace cppx::process::detail

export namespace cppx::process::system {

inline auto run(cppx::process::ProcessSpec const& spec)
    -> std::expected<cppx::process::ProcessResult, cppx::process::process_error> {
    if (spec.program.empty())
        return std::unexpected{cppx::process::process_error::empty_program};
    return cppx::process::detail::run_process(spec);
}

} // namespace cppx::process::system
