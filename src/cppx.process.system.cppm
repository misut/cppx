// System process runner. Public wrappers stay tiny while platform
// spawn/capture details live in dedicated Windows/POSIX helpers below.

module;

#if defined(__APPLE__) || defined(__linux__)
#include <cstdlib>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
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

struct completed_process {
    cppx::process::ProcessResult result;
    std::string stdout_text;
    std::string stderr_text;
};

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

namespace windows {

struct capture_pipes {
    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;
    HANDLE stderr_read = nullptr;
    HANDLE stderr_write = nullptr;
};

inline auto close_handle(HANDLE& handle) -> void {
    if (handle != nullptr) {
        CloseHandle(handle);
        handle = nullptr;
    }
}

inline auto make_inheritable_pipe() -> std::expected<std::pair<HANDLE, HANDLE>, cppx::process::process_error> {
    SECURITY_ATTRIBUTES attrs{};
    attrs.nLength = sizeof(attrs);
    attrs.bInheritHandle = TRUE;

    HANDLE read_handle = nullptr;
    HANDLE write_handle = nullptr;
    if (!CreatePipe(&read_handle, &write_handle, &attrs, 0))
        return std::unexpected{cppx::process::process_error::spawn_failed};
    if (!SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(read_handle);
        CloseHandle(write_handle);
        return std::unexpected{cppx::process::process_error::spawn_failed};
    }
    return std::pair{read_handle, write_handle};
}

inline auto make_capture_pipes() -> std::expected<capture_pipes, cppx::process::process_error> {
    auto stdout_pair = make_inheritable_pipe();
    if (!stdout_pair)
        return std::unexpected{stdout_pair.error()};

    auto stderr_pair = make_inheritable_pipe();
    if (!stderr_pair) {
        auto [read_handle, write_handle] = *stdout_pair;
        CloseHandle(read_handle);
        CloseHandle(write_handle);
        return std::unexpected{stderr_pair.error()};
    }

    return capture_pipes{
        .stdout_read = stdout_pair->first,
        .stdout_write = stdout_pair->second,
        .stderr_read = stderr_pair->first,
        .stderr_write = stderr_pair->second,
    };
}

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
        for (auto* cursor = env_block; *cursor != L'\0';
             cursor += std::wcslen(cursor) + 1) {
            vars.emplace_back(cursor);
        }
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

inline auto read_pipe(HANDLE handle, std::string& out) -> void {
    auto buffer = std::array<char, 4096>{};
    while (true) {
        DWORD bytes_read = 0;
        auto ok = ReadFile(
            handle,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            &bytes_read,
            nullptr);
        if (!ok) {
            auto error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_OPERATION_ABORTED)
                break;
            break;
        }
        if (bytes_read == 0)
            break;
        out.append(buffer.data(), bytes_read);
    }
    CloseHandle(handle);
}

inline auto run_process(cppx::process::ProcessSpec const& spec,
                        bool capture_output)
    -> std::expected<completed_process, cppx::process::process_error> {
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

    auto pipes = capture_pipes{};
    if (capture_output) {
        auto created = make_capture_pipes();
        if (!created)
            return std::unexpected{created.error()};
        pipes = *created;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (capture_output) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = pipes.stdout_write;
        si.hStdError = pipes.stderr_write;
    }

    PROCESS_INFORMATION pi{};
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(
                job,
                JobObjectExtendedLimitInformation,
                &info,
                sizeof(info))) {
            CloseHandle(job);
            job = nullptr;
        }
    }

    auto old_error_mode =
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    auto restore_error_mode = [&] { SetErrorMode(old_error_mode); };

    auto creation_flags =
        environment->empty() ? 0U : static_cast<unsigned>(CREATE_UNICODE_ENVIRONMENT);

    auto created = CreateProcessW(
        nullptr,
        mutable_command.data(),
        nullptr,
        nullptr,
        capture_output ? TRUE : FALSE,
        creation_flags,
        environment->empty() ? nullptr : environment->data(),
        cwd ? cwd->c_str() : nullptr,
        &si,
        &pi);

    if (!created) {
        auto error = GetLastError();
        close_handle(pipes.stdout_read);
        close_handle(pipes.stdout_write);
        close_handle(pipes.stderr_read);
        close_handle(pipes.stderr_write);
        if (job != nullptr)
            CloseHandle(job);
        restore_error_mode();
        if (error == ERROR_DIRECTORY)
            return std::unexpected{cppx::process::process_error::cwd_unavailable};
        return std::unexpected{cppx::process::process_error::spawn_failed};
    }

    if (capture_output) {
        close_handle(pipes.stdout_write);
        close_handle(pipes.stderr_write);
    }

    if (job != nullptr && !AssignProcessToJobObject(job, pi.hProcess)) {
        CloseHandle(job);
        job = nullptr;
    }

    auto stdout_text = std::string{};
    auto stderr_text = std::string{};
    auto stdout_reader = std::thread{};
    auto stderr_reader = std::thread{};
    if (capture_output) {
        stdout_reader = std::thread(read_pipe, pipes.stdout_read, std::ref(stdout_text));
        stderr_reader = std::thread(read_pipe, pipes.stderr_read, std::ref(stderr_text));
        pipes.stdout_read = nullptr;
        pipes.stderr_read = nullptr;
    }

    auto wait_ms = spec.timeout
        ? static_cast<DWORD>(std::min<std::chrono::milliseconds::rep>(
              spec.timeout->count(), std::numeric_limits<DWORD>::max()))
        : INFINITE;
    auto wait_result = WaitForSingleObject(pi.hProcess, wait_ms);

    auto result = cppx::process::ProcessResult{};
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
        if (stdout_reader.joinable())
            stdout_reader.join();
        if (stderr_reader.joinable())
            stderr_reader.join();
        close_handle(pipes.stdout_read);
        close_handle(pipes.stderr_read);
        return std::unexpected{cppx::process::process_error::wait_failed};
    } else {
        DWORD exit_code = 0;
        if (!GetExitCodeProcess(pi.hProcess, &exit_code)) {
            if (job != nullptr)
                CloseHandle(job);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            restore_error_mode();
            if (stdout_reader.joinable())
                stdout_reader.join();
            if (stderr_reader.joinable())
                stderr_reader.join();
            close_handle(pipes.stdout_read);
            close_handle(pipes.stderr_read);
            return std::unexpected{cppx::process::process_error::wait_failed};
        }
        result.exit_code = static_cast<int>(exit_code);
    }

    if (stdout_reader.joinable())
        stdout_reader.join();
    if (stderr_reader.joinable())
        stderr_reader.join();

    close_handle(pipes.stdout_read);
    close_handle(pipes.stderr_read);
    if (job != nullptr)
        CloseHandle(job);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    restore_error_mode();

    return completed_process{
        .result = result,
        .stdout_text = std::move(stdout_text),
        .stderr_text = std::move(stderr_text),
    };
}

} // namespace windows

#elif defined(__APPLE__) || defined(__linux__)

namespace posix {

struct child_failure {
    char stage = 'x';
    int error = 0;
};

struct pipe_pair {
    int read_end = -1;
    int write_end = -1;
};

inline auto close_fd(int& fd) -> void {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

inline auto set_cloexec(int fd) -> void {
    auto flags = ::fcntl(fd, F_GETFD);
    if (flags >= 0)
        ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

inline auto set_nonblocking(int fd) -> void {
    auto flags = ::fcntl(fd, F_GETFL);
    if (flags >= 0)
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

inline auto make_pipe_pair(bool nonblocking_read = false)
    -> std::expected<pipe_pair, cppx::process::process_error> {
    std::array<int, 2> fds{-1, -1};
    if (::pipe(fds.data()) != 0)
        return std::unexpected{cppx::process::process_error::spawn_failed};
    set_cloexec(fds[1]);
    if (nonblocking_read)
        set_nonblocking(fds[0]);
    return pipe_pair{
        .read_end = fds[0],
        .write_end = fds[1],
    };
}

inline auto drain_fd(int& fd, std::string& out) -> bool {
    auto buffer = std::array<char, 4096>{};
    while (fd >= 0) {
        auto n = ::read(fd, buffer.data(), buffer.size());
        if (n > 0) {
            out.append(buffer.data(), static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) {
            close_fd(fd);
            return true;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return true;
        close_fd(fd);
        return false;
    }
    return true;
}

inline auto run_process(cppx::process::ProcessSpec const& spec,
                        bool capture_output)
    -> std::expected<completed_process, cppx::process::process_error> {
    auto error_pipe = make_pipe_pair();
    if (!error_pipe)
        return std::unexpected{error_pipe.error()};

    auto stdout_pipe = pipe_pair{};
    auto stderr_pipe = pipe_pair{};
    if (capture_output) {
        auto created_stdout = make_pipe_pair(true);
        if (!created_stdout) {
            auto pair = *error_pipe;
            close_fd(pair.read_end);
            close_fd(pair.write_end);
            return std::unexpected{created_stdout.error()};
        }
        auto created_stderr = make_pipe_pair(true);
        if (!created_stderr) {
            auto pair = *error_pipe;
            close_fd(pair.read_end);
            close_fd(pair.write_end);
            stdout_pipe = *created_stdout;
            close_fd(stdout_pipe.read_end);
            close_fd(stdout_pipe.write_end);
            return std::unexpected{created_stderr.error()};
        }
        stdout_pipe = *created_stdout;
        stderr_pipe = *created_stderr;
    }

    auto argv_storage = std::vector<std::string>{spec.program};
    argv_storage.insert(argv_storage.end(), spec.args.begin(), spec.args.end());
    auto argv = std::vector<char*>{};
    argv.reserve(argv_storage.size() + 1);
    for (auto& arg : argv_storage)
        argv.push_back(arg.data());
    argv.push_back(nullptr);

    auto pid = ::fork();
    if (pid < 0) {
        auto pair = *error_pipe;
        close_fd(pair.read_end);
        close_fd(pair.write_end);
        close_fd(stdout_pipe.read_end);
        close_fd(stdout_pipe.write_end);
        close_fd(stderr_pipe.read_end);
        close_fd(stderr_pipe.write_end);
        return std::unexpected{cppx::process::process_error::spawn_failed};
    }

    if (pid == 0) {
        auto pair = *error_pipe;
        close_fd(pair.read_end);
        close_fd(stdout_pipe.read_end);
        close_fd(stderr_pipe.read_end);

        if (capture_output) {
            if (::dup2(stdout_pipe.write_end, STDOUT_FILENO) < 0) {
                auto failure = child_failure{.stage = 'o', .error = errno};
                [[maybe_unused]] auto ignored =
                    ::write(pair.write_end, &failure, sizeof(failure));
                _exit(127);
            }
            if (::dup2(stderr_pipe.write_end, STDERR_FILENO) < 0) {
                auto failure = child_failure{.stage = 's', .error = errno};
                [[maybe_unused]] auto ignored =
                    ::write(pair.write_end, &failure, sizeof(failure));
                _exit(127);
            }
            close_fd(stdout_pipe.write_end);
            close_fd(stderr_pipe.write_end);
        }

        if (!spec.cwd.empty() && ::chdir(spec.cwd.c_str()) != 0) {
            auto failure = child_failure{.stage = 'c', .error = errno};
            [[maybe_unused]] auto ignored =
                ::write(pair.write_end, &failure, sizeof(failure));
            _exit(127);
        }

        for (auto const& [name, value] : spec.env_overrides) {
            if (::setenv(name.c_str(), value.c_str(), 1) != 0) {
                auto failure = child_failure{.stage = 'e', .error = errno};
                [[maybe_unused]] auto ignored =
                    ::write(pair.write_end, &failure, sizeof(failure));
                _exit(127);
            }
        }

        ::execvp(argv[0], argv.data());
        auto failure = child_failure{.stage = 'x', .error = errno};
        [[maybe_unused]] auto ignored =
            ::write(pair.write_end, &failure, sizeof(failure));
        _exit(127);
    }

    auto pair = *error_pipe;
    close_fd(pair.write_end);
    close_fd(stdout_pipe.write_end);
    close_fd(stderr_pipe.write_end);

    auto stdout_text = std::string{};
    auto stderr_text = std::string{};
    auto result = cppx::process::ProcessResult{};

    bool child_finished = false;
    int status = 0;
    auto deadline = spec.timeout
        ? std::optional{std::chrono::steady_clock::now() + *spec.timeout}
        : std::nullopt;

    while (!child_finished || stdout_pipe.read_end >= 0 || stderr_pipe.read_end >= 0) {
        if (!child_finished) {
            auto rc = ::waitpid(pid, &status, WNOHANG);
            if (rc == pid) {
                child_finished = true;
                result.exit_code = normalize_exit_status(status);
            } else if (rc < 0) {
                if (errno != EINTR) {
                    close_fd(pair.read_end);
                    close_fd(stdout_pipe.read_end);
                    close_fd(stderr_pipe.read_end);
                    return std::unexpected{cppx::process::process_error::wait_failed};
                }
            }
        }

        if (!child_finished && deadline &&
            std::chrono::steady_clock::now() >= *deadline) {
            ::kill(pid, SIGKILL);
            while (::waitpid(pid, &status, 0) < 0) {
                if (errno != EINTR) {
                    close_fd(pair.read_end);
                    close_fd(stdout_pipe.read_end);
                    close_fd(stderr_pipe.read_end);
                    return std::unexpected{cppx::process::process_error::wait_failed};
                }
            }
            child_finished = true;
            result = cppx::process::ProcessResult{
                .exit_code = 124,
                .timed_out = true,
            };
        }

        if (capture_output && (stdout_pipe.read_end >= 0 || stderr_pipe.read_end >= 0)) {
            auto pollfds = std::array<pollfd, 2>{};
            nfds_t nfds = 0;
            if (stdout_pipe.read_end >= 0) {
                pollfds[nfds++] = {
                    .fd = stdout_pipe.read_end,
                    .events = POLLIN | POLLHUP,
                    .revents = 0,
                };
            }
            if (stderr_pipe.read_end >= 0) {
                pollfds[nfds++] = {
                    .fd = stderr_pipe.read_end,
                    .events = POLLIN | POLLHUP,
                    .revents = 0,
                };
            }

            auto timeout_ms = 0;
            if (!child_finished) {
                if (deadline) {
                    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                        *deadline - std::chrono::steady_clock::now());
                    timeout_ms = static_cast<int>(std::clamp<std::int64_t>(
                        remaining.count(),
                        0,
                        50));
                } else {
                    timeout_ms = 50;
                }
            }

            auto rc = ::poll(pollfds.data(), nfds, timeout_ms);
            if (rc < 0) {
                if (errno != EINTR) {
                    close_fd(pair.read_end);
                    close_fd(stdout_pipe.read_end);
                    close_fd(stderr_pipe.read_end);
                    return std::unexpected{cppx::process::process_error::wait_failed};
                }
            } else if (rc > 0) {
                if (stdout_pipe.read_end >= 0 && !drain_fd(stdout_pipe.read_end, stdout_text)) {
                    close_fd(pair.read_end);
                    close_fd(stderr_pipe.read_end);
                    return std::unexpected{cppx::process::process_error::wait_failed};
                }
                if (stderr_pipe.read_end >= 0 && !drain_fd(stderr_pipe.read_end, stderr_text)) {
                    close_fd(pair.read_end);
                    return std::unexpected{cppx::process::process_error::wait_failed};
                }
            }

            if (child_finished) {
                if (stdout_pipe.read_end >= 0 && !drain_fd(stdout_pipe.read_end, stdout_text)) {
                    close_fd(pair.read_end);
                    close_fd(stderr_pipe.read_end);
                    return std::unexpected{cppx::process::process_error::wait_failed};
                }
                if (stderr_pipe.read_end >= 0 && !drain_fd(stderr_pipe.read_end, stderr_text)) {
                    close_fd(pair.read_end);
                    return std::unexpected{cppx::process::process_error::wait_failed};
                }
            }
        } else if (!child_finished) {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        } else {
            break;
        }
    }

    auto failure = child_failure{};
    auto failure_size = ::read(pair.read_end, &failure, sizeof(failure));
    close_fd(pair.read_end);

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

    return completed_process{
        .result = result,
        .stdout_text = std::move(stdout_text),
        .stderr_text = std::move(stderr_text),
    };
}

} // namespace posix

#else
#endif

inline auto run_process(cppx::process::ProcessSpec const& spec, bool capture_output)
    -> std::expected<completed_process, cppx::process::process_error> {
#if defined(_WIN32)
    return windows::run_process(spec, capture_output);
#elif defined(__APPLE__) || defined(__linux__)
    return posix::run_process(spec, capture_output);
#else
    (void)spec;
    (void)capture_output;
    return std::unexpected{cppx::process::process_error::unsupported};
#endif
}

} // namespace cppx::process::detail

export namespace cppx::process::system {

class ChildProcess {
public:
    struct state;

    ChildProcess() = default;

    ChildProcess(ChildProcess const&) = delete;
    auto operator=(ChildProcess const&) -> ChildProcess& = delete;

    ChildProcess(ChildProcess&& other) noexcept
        : state_{std::move(other.state_)} {}

    auto operator=(ChildProcess&& other) noexcept -> ChildProcess& {
        if (this != &other) {
            close();
            state_ = std::move(other.state_);
        }
        return *this;
    }

    ~ChildProcess() {
        close();
    }

    bool valid() const {
        return state_ != nullptr;
    }

    std::optional<cppx::process::ProcessEvent> try_event();
    std::optional<cppx::process::ProcessEvent> wait_event();
    bool write_stdin(std::string_view text);
    bool terminate();
    void close();

private:
    explicit ChildProcess(std::shared_ptr<state> state)
        : state_{std::move(state)} {}

    friend auto spawn(cppx::process::ProcessStreamSpec spec)
        -> std::expected<ChildProcess, cppx::process::process_error>;

    std::shared_ptr<state> state_;
};

auto spawn(cppx::process::ProcessStreamSpec spec)
    -> std::expected<ChildProcess, cppx::process::process_error>;

inline auto run(cppx::process::ProcessSpec const& spec)
    -> std::expected<cppx::process::ProcessResult, cppx::process::process_error> {
    if (spec.program.empty())
        return std::unexpected{cppx::process::process_error::empty_program};

    auto result = cppx::process::detail::run_process(spec, false);
    if (!result)
        return std::unexpected{result.error()};
    return result->result;
}

inline auto capture(cppx::process::ProcessSpec const& spec)
    -> std::expected<cppx::process::CapturedProcessResult, cppx::process::process_error> {
    if (spec.program.empty())
        return std::unexpected{cppx::process::process_error::empty_program};

    auto result = cppx::process::detail::run_process(spec, true);
    if (!result)
        return std::unexpected{result.error()};

    return cppx::process::CapturedProcessResult{
        .exit_code = result->result.exit_code,
        .timed_out = result->result.timed_out,
        .stdout_text = std::move(result->stdout_text),
        .stderr_text = std::move(result->stderr_text),
    };
}

} // namespace cppx::process::system

namespace cppx::process::system {

struct ChildProcess::state {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<cppx::process::ProcessEvent> events;
    std::thread worker;
    std::atomic<bool> stop_requested = false;
    bool finished = false;
#if defined(__APPLE__) || defined(__linux__)
    std::mutex pid_mutex;
    pid_t pid = -1;
#endif

    void push(cppx::process::ProcessEvent event) {
        {
            auto lock = std::lock_guard{mutex};
            events.push_back(std::move(event));
        }
        cv.notify_all();
    }
};

namespace stream_detail {

inline void append_limited(std::string_view text,
                           std::size_t limit,
                           std::string& out) {
    if (out.size() >= limit)
        return;
    auto available = limit - out.size();
    out.append(text.substr(0, available));
}

inline void push_captured_chunks(ChildProcess::state& state,
                                 cppx::process::CapturedProcessResult const& result,
                                 std::size_t output_limit) {
    auto stdout_text = std::string{};
    auto stderr_text = std::string{};
    append_limited(result.stdout_text, output_limit, stdout_text);
    append_limited(result.stderr_text, output_limit, stderr_text);

    if (!stdout_text.empty()) {
        state.push({
            .kind = cppx::process::ProcessEventKind::stdout_chunk,
            .text = std::move(stdout_text),
        });
    }
    if (!stderr_text.empty()) {
        state.push({
            .kind = cppx::process::ProcessEventKind::stderr_chunk,
            .text = std::move(stderr_text),
        });
    }
    state.push({
        .kind = cppx::process::ProcessEventKind::exited,
        .exit_code = result.exit_code,
        .timed_out = result.timed_out,
    });
}

#if defined(__APPLE__) || defined(__linux__)
inline void push_limited_chunk(ChildProcess::state& state,
                               cppx::process::ProcessEventKind kind,
                               std::string_view text,
                               std::size_t output_limit,
                               std::size_t& emitted) {
    if (text.empty() || emitted >= output_limit)
        return;
    auto available = output_limit - emitted;
    auto chunk = std::string{text.substr(0, available)};
    emitted += chunk.size();
    state.push({
        .kind = kind,
        .text = std::move(chunk),
    });
}

inline bool drain_stream_fd(int& fd,
                            ChildProcess::state& state,
                            cppx::process::ProcessEventKind kind,
                            std::size_t output_limit,
                            std::size_t& emitted) {
    auto buffer = std::array<char, 4096>{};
    while (fd >= 0) {
        auto n = ::read(fd, buffer.data(), buffer.size());
        if (n > 0) {
            push_limited_chunk(
                state,
                kind,
                std::string_view{buffer.data(), static_cast<std::size_t>(n)},
                output_limit,
                emitted);
            continue;
        }
        if (n == 0) {
            cppx::process::detail::posix::close_fd(fd);
            return true;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return true;
        cppx::process::detail::posix::close_fd(fd);
        return false;
    }
    return true;
}
#endif

} // namespace stream_detail

std::optional<cppx::process::ProcessEvent> ChildProcess::try_event() {
    if (!state_)
        return std::nullopt;

    auto lock = std::lock_guard{state_->mutex};
    if (state_->events.empty())
        return std::nullopt;

    auto event = std::optional<cppx::process::ProcessEvent>{
        std::move(state_->events.front())};
    state_->events.pop_front();
    return event;
}

std::optional<cppx::process::ProcessEvent> ChildProcess::wait_event() {
    if (!state_)
        return std::nullopt;

    auto lock = std::unique_lock{state_->mutex};
    state_->cv.wait(lock, [&] {
        return !state_->events.empty() || state_->finished;
    });

    if (state_->events.empty())
        return std::nullopt;

    auto event = std::optional<cppx::process::ProcessEvent>{
        std::move(state_->events.front())};
    state_->events.pop_front();
    return event;
}

bool ChildProcess::write_stdin(std::string_view text) {
    (void)text;
    return false;
}

bool ChildProcess::terminate() {
    if (!state_)
        return false;
    state_->stop_requested.store(true, std::memory_order_relaxed);
#if defined(__APPLE__) || defined(__linux__)
    auto lock = std::lock_guard{state_->pid_mutex};
    if (state_->pid > 0)
        return ::kill(state_->pid, SIGTERM) == 0;
#endif
    return false;
}

void ChildProcess::close() {
    if (!state_)
        return;
    state_->stop_requested.store(true, std::memory_order_relaxed);
    terminate();
    if (state_->worker.joinable())
        state_->worker.join();
    state_.reset();
}

auto spawn(cppx::process::ProcessStreamSpec spec)
    -> std::expected<ChildProcess, cppx::process::process_error> {
    if (spec.program.empty())
        return std::unexpected{cppx::process::process_error::empty_program};

#if defined(__APPLE__) || defined(__linux__)
    namespace posix = cppx::process::detail::posix;

    auto error_pipe = posix::make_pipe_pair();
    if (!error_pipe)
        return std::unexpected{error_pipe.error()};

    auto stdout_pipe = posix::make_pipe_pair(true);
    if (!stdout_pipe) {
        auto pair = *error_pipe;
        posix::close_fd(pair.read_end);
        posix::close_fd(pair.write_end);
        return std::unexpected{stdout_pipe.error()};
    }
    auto stderr_pipe = posix::make_pipe_pair(true);
    if (!stderr_pipe) {
        auto pair = *error_pipe;
        auto out = *stdout_pipe;
        posix::close_fd(pair.read_end);
        posix::close_fd(pair.write_end);
        posix::close_fd(out.read_end);
        posix::close_fd(out.write_end);
        return std::unexpected{stderr_pipe.error()};
    }

    auto argv_storage = std::vector<std::string>{spec.program};
    argv_storage.insert(argv_storage.end(), spec.args.begin(), spec.args.end());
    auto argv = std::vector<char*>{};
    argv.reserve(argv_storage.size() + 1);
    for (auto& arg : argv_storage)
        argv.push_back(arg.data());
    argv.push_back(nullptr);

    auto pid = ::fork();
    if (pid < 0) {
        auto pair = *error_pipe;
        auto out = *stdout_pipe;
        auto err = *stderr_pipe;
        posix::close_fd(pair.read_end);
        posix::close_fd(pair.write_end);
        posix::close_fd(out.read_end);
        posix::close_fd(out.write_end);
        posix::close_fd(err.read_end);
        posix::close_fd(err.write_end);
        return std::unexpected{cppx::process::process_error::spawn_failed};
    }

    if (pid == 0) {
        auto pair = *error_pipe;
        auto out = *stdout_pipe;
        auto err = *stderr_pipe;
        posix::close_fd(pair.read_end);
        posix::close_fd(out.read_end);
        posix::close_fd(err.read_end);

        if (::dup2(out.write_end, STDOUT_FILENO) < 0) {
            auto failure = posix::child_failure{.stage = 'o', .error = errno};
            [[maybe_unused]] auto ignored =
                ::write(pair.write_end, &failure, sizeof(failure));
            _exit(127);
        }
        if (::dup2(err.write_end, STDERR_FILENO) < 0) {
            auto failure = posix::child_failure{.stage = 's', .error = errno};
            [[maybe_unused]] auto ignored =
                ::write(pair.write_end, &failure, sizeof(failure));
            _exit(127);
        }
        posix::close_fd(out.write_end);
        posix::close_fd(err.write_end);

        if (!spec.cwd.empty() && ::chdir(spec.cwd.c_str()) != 0) {
            auto failure = posix::child_failure{.stage = 'c', .error = errno};
            [[maybe_unused]] auto ignored =
                ::write(pair.write_end, &failure, sizeof(failure));
            _exit(127);
        }
        for (auto const& [name, value] : spec.env_overrides) {
            if (::setenv(name.c_str(), value.c_str(), 1) != 0) {
                auto failure = posix::child_failure{.stage = 'e', .error = errno};
                [[maybe_unused]] auto ignored =
                    ::write(pair.write_end, &failure, sizeof(failure));
                _exit(127);
            }
        }

        ::execvp(argv[0], argv.data());
        auto failure = posix::child_failure{.stage = 'x', .error = errno};
        [[maybe_unused]] auto ignored =
            ::write(pair.write_end, &failure, sizeof(failure));
        _exit(127);
    }

    auto pair = *error_pipe;
    auto out = *stdout_pipe;
    auto err = *stderr_pipe;
    posix::close_fd(pair.write_end);
    posix::close_fd(out.write_end);
    posix::close_fd(err.write_end);

    auto state = std::make_shared<ChildProcess::state>();
    state->pid = pid;
    state->worker = std::thread{[
        state,
        pid,
        pair,
        out,
        err,
        spec = std::move(spec)
    ] {
        auto error_read = pair.read_end;
        auto stdout_read = out.read_end;
        auto stderr_read = err.read_end;
        auto output_limit = spec.output_limit;
        auto emitted = std::size_t{0};
        auto result = cppx::process::ProcessResult{};
        auto child_finished = false;
        auto failure = posix::child_failure{};
        auto failure_size = ssize_t{0};
        auto deadline = spec.timeout
            ? std::optional{std::chrono::steady_clock::now() + *spec.timeout}
            : std::nullopt;

        while (!child_finished || stdout_read >= 0 || stderr_read >= 0) {
            if (!child_finished) {
                int status = 0;
                auto waited = ::waitpid(pid, &status, WNOHANG);
                if (waited == pid) {
                    child_finished = true;
                    result.exit_code = cppx::process::detail::normalize_exit_status(status);
                    {
                        auto lock = std::lock_guard{state->pid_mutex};
                        state->pid = -1;
                    }
                } else if (waited < 0 && errno != EINTR) {
                    state->push({
                        .kind = cppx::process::ProcessEventKind::failed,
                        .error = cppx::process::process_error::wait_failed,
                    });
                    break;
                }
            }

            if (!child_finished && deadline &&
                std::chrono::steady_clock::now() >= *deadline) {
                (void)::kill(pid, SIGKILL);
                int status = 0;
                while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
                child_finished = true;
                result.exit_code = 124;
                result.timed_out = true;
                {
                    auto lock = std::lock_guard{state->pid_mutex};
                    state->pid = -1;
                }
            }

            auto pollfds = std::array<pollfd, 2>{};
            nfds_t nfds = 0;
            if (stdout_read >= 0) {
                pollfds[nfds++] = {
                    .fd = stdout_read,
                    .events = POLLIN | POLLHUP,
                    .revents = 0,
                };
            }
            if (stderr_read >= 0) {
                pollfds[nfds++] = {
                    .fd = stderr_read,
                    .events = POLLIN | POLLHUP,
                    .revents = 0,
                };
            }

            if (nfds > 0) {
                auto timeout_ms = 0;
                if (!child_finished) {
                    timeout_ms = 50;
                    if (deadline) {
                        auto remaining = std::chrono::duration_cast<
                            std::chrono::milliseconds>(
                            *deadline - std::chrono::steady_clock::now());
                        timeout_ms = static_cast<int>(std::clamp<std::int64_t>(
                            remaining.count(),
                            0,
                            50));
                    }
                }

                auto rc = ::poll(pollfds.data(), nfds, timeout_ms);
                if (rc > 0) {
                    if (stdout_read >= 0)
                        (void)stream_detail::drain_stream_fd(
                            stdout_read,
                            *state,
                            cppx::process::ProcessEventKind::stdout_chunk,
                            output_limit,
                            emitted);
                    if (stderr_read >= 0)
                        (void)stream_detail::drain_stream_fd(
                            stderr_read,
                            *state,
                            cppx::process::ProcessEventKind::stderr_chunk,
                            output_limit,
                            emitted);
                }
            } else if (!child_finished) {
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
            } else {
                break;
            }
        }

        if (error_read >= 0) {
            failure_size = ::read(error_read, &failure, sizeof(failure));
            posix::close_fd(error_read);
        }
        posix::close_fd(stdout_read);
        posix::close_fd(stderr_read);

        if (failure_size == static_cast<ssize_t>(sizeof(failure))) {
            auto error = cppx::process::process_error::spawn_failed;
            if (failure.stage == 'c')
                error = cppx::process::process_error::cwd_unavailable;
            else if (failure.stage == 'e')
                error = cppx::process::process_error::environment_failed;
            state->push({
                .kind = cppx::process::ProcessEventKind::failed,
                .error = error,
            });
        } else {
            state->push({
                .kind = cppx::process::ProcessEventKind::exited,
                .exit_code = result.exit_code,
                .timed_out = result.timed_out,
            });
        }

        {
            auto lock = std::lock_guard{state->mutex};
            state->finished = true;
        }
        state->cv.notify_all();
    }};

    return ChildProcess{std::move(state)};
#else
    auto state = std::make_shared<ChildProcess::state>();
    state->worker = std::thread{[state, spec = std::move(spec)] {
        auto captured = cppx::process::system::capture(spec);
        if (!captured) {
            state->push({
                .kind = cppx::process::ProcessEventKind::failed,
                .error = captured.error(),
            });
        } else {
            stream_detail::push_captured_chunks(
                *state,
                *captured,
                spec.output_limit);
        }

        {
            auto lock = std::lock_guard{state->mutex};
            state->finished = true;
        }
        state->cv.notify_all();
    }};

    return ChildProcess{std::move(state)};
#endif
}

} // namespace cppx::process::system
