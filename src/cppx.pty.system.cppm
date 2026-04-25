// Minimal pseudo-terminal wrapper for interactive CLI tests and tools.

module;
#if defined(__APPLE__)
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <util.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

export module cppx.pty.system;
import std;
import cppx.process;

export namespace cppx::pty::system {

struct PtySize {
    std::size_t columns = 80;
    std::size_t rows = 24;
};

struct PtySpec {
    std::string program;
    std::vector<std::string> args;
    std::filesystem::path cwd;
    PtySize size;
};

enum class PtyEventKind {
    output,
    exited,
    failed,
};

struct PtyEvent {
    PtyEventKind kind = PtyEventKind::output;
    std::string text;
    int exit_code = 0;
    std::optional<cppx::process::process_error> error;
};

class PtySession {
public:
    struct state;

    PtySession() = default;
    PtySession(PtySession const&) = delete;
    auto operator=(PtySession const&) -> PtySession& = delete;
    PtySession(PtySession&& other) noexcept
        : state_{std::move(other.state_)} {}
    auto operator=(PtySession&& other) noexcept -> PtySession& {
        if (this != &other) {
            close();
            state_ = std::move(other.state_);
        }
        return *this;
    }
    ~PtySession() { close(); }

    bool valid() const;
    bool write(std::string_view text);
    std::optional<PtyEvent> read_event(std::chrono::milliseconds timeout);
    bool resize(PtySize size);
    void terminate();
    void close();

private:
    explicit PtySession(std::shared_ptr<state> state)
        : state_{std::move(state)} {}
    friend auto spawn(PtySpec spec)
        -> std::expected<PtySession, cppx::process::process_error>;
    std::shared_ptr<state> state_;
};

auto spawn(PtySpec spec)
    -> std::expected<PtySession, cppx::process::process_error>;

} // namespace cppx::pty::system

namespace cppx::pty::system {

struct PtySession::state {
#if defined(__APPLE__) || defined(__linux__)
    int fd = -1;
    pid_t pid = -1;
#endif
    bool exited = false;
    int exit_code = 0;
};

#if defined(__APPLE__) || defined(__linux__)

namespace detail {

inline int normalize_exit_status(int status) noexcept {
    if (status == -1)
        return 1;
    if ((status & 0x7f) == 0)
        return (status >> 8) & 0xff;
    if ((status & 0x7f) != 0x7f)
        return 128 + (status & 0x7f);
    return status;
}

inline void close_fd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

inline void set_nonblocking(int fd) {
    auto flags = ::fcntl(fd, F_GETFL);
    if (flags >= 0)
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

} // namespace detail

#endif

bool PtySession::valid() const {
#if defined(__APPLE__) || defined(__linux__)
    return state_ && state_->fd >= 0;
#else
    return false;
#endif
}

bool PtySession::write(std::string_view text) {
#if defined(__APPLE__) || defined(__linux__)
    if (!valid())
        return false;
    while (!text.empty()) {
        auto n = ::write(state_->fd, text.data(), text.size());
        if (n <= 0)
            return false;
        text.remove_prefix(static_cast<std::size_t>(n));
    }
    return true;
#else
    (void)text;
    return false;
#endif
}

std::optional<PtyEvent> PtySession::read_event(std::chrono::milliseconds timeout) {
#if defined(__APPLE__) || defined(__linux__)
    if (!state_)
        return std::nullopt;

    if (!state_->exited && state_->pid > 0) {
        int status = 0;
        auto waited = ::waitpid(state_->pid, &status, WNOHANG);
        if (waited == state_->pid) {
            state_->exited = true;
            state_->exit_code = detail::normalize_exit_status(status);
            return PtyEvent{
                .kind = PtyEventKind::exited,
                .exit_code = state_->exit_code,
            };
        }
    }

    if (state_->fd < 0)
        return std::nullopt;

    auto pfd = pollfd{
        .fd = state_->fd,
        .events = POLLIN | POLLHUP,
        .revents = 0,
    };
    auto timeout_ms = static_cast<int>(
        std::clamp<long long>(timeout.count(), 0, std::numeric_limits<int>::max()));
    auto rc = ::poll(&pfd, 1, timeout_ms);
    if (rc <= 0)
        return std::nullopt;

    auto buffer = std::array<char, 4096>{};
    auto n = ::read(state_->fd, buffer.data(), buffer.size());
    if (n > 0) {
        return PtyEvent{
            .kind = PtyEventKind::output,
            .text = std::string{buffer.data(), static_cast<std::size_t>(n)},
        };
    }
    return std::nullopt;
#else
    (void)timeout;
    return PtyEvent{
        .kind = PtyEventKind::failed,
        .error = cppx::process::process_error::unsupported,
    };
#endif
}

bool PtySession::resize(PtySize size) {
#if defined(__APPLE__) || defined(__linux__)
    if (!valid())
        return false;
    auto win = winsize{
        .ws_row = static_cast<unsigned short>(size.rows),
        .ws_col = static_cast<unsigned short>(size.columns),
        .ws_xpixel = 0,
        .ws_ypixel = 0,
    };
    return ::ioctl(state_->fd, TIOCSWINSZ, &win) == 0;
#else
    (void)size;
    return false;
#endif
}

void PtySession::terminate() {
#if defined(__APPLE__) || defined(__linux__)
    if (state_ && state_->pid > 0 && !state_->exited)
        ::kill(state_->pid, SIGTERM);
#endif
}

void PtySession::close() {
#if defined(__APPLE__) || defined(__linux__)
    if (!state_)
        return;
    terminate();
    if (state_->pid > 0 && !state_->exited) {
        int status = 0;
        (void)::waitpid(state_->pid, &status, 0);
        state_->exited = true;
    }
    detail::close_fd(state_->fd);
#endif
    state_.reset();
}

auto spawn(PtySpec spec)
    -> std::expected<PtySession, cppx::process::process_error> {
    if (spec.program.empty())
        return std::unexpected{cppx::process::process_error::empty_program};

#if defined(__APPLE__) || defined(__linux__)
    auto argv_storage = std::vector<std::string>{spec.program};
    argv_storage.insert(argv_storage.end(), spec.args.begin(), spec.args.end());
    auto argv = std::vector<char*>{};
    argv.reserve(argv_storage.size() + 1);
    for (auto& arg : argv_storage)
        argv.push_back(arg.data());
    argv.push_back(nullptr);

    auto win = winsize{
        .ws_row = static_cast<unsigned short>(spec.size.rows),
        .ws_col = static_cast<unsigned short>(spec.size.columns),
        .ws_xpixel = 0,
        .ws_ypixel = 0,
    };

    int fd = -1;
    auto pid = ::forkpty(&fd, nullptr, nullptr, &win);
    if (pid < 0)
        return std::unexpected{cppx::process::process_error::spawn_failed};

    if (pid == 0) {
        if (!spec.cwd.empty())
            (void)::chdir(spec.cwd.c_str());
        ::execvp(argv[0], argv.data());
        _exit(127);
    }

    detail::set_nonblocking(fd);
    auto state = std::make_shared<PtySession::state>();
    state->fd = fd;
    state->pid = pid;
    return PtySession{std::move(state)};
#else
    (void)spec;
    return std::unexpected{cppx::process::process_error::unsupported};
#endif
}

} // namespace cppx::pty::system
