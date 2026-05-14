// Process-facing data and error types. Spawning, waiting, and pipe I/O
// stay in cppx.process.system.

export module cppx.process;
import std;

export namespace cppx::process {

enum class process_error {
    empty_program,
    cwd_unavailable,
    environment_failed,
    spawn_failed,
    wait_failed,
    encoding_failed,
    unsupported,
};

inline constexpr auto to_string(process_error error) -> std::string_view {
    switch (error) {
    case process_error::empty_program:
        return "empty_program";
    case process_error::cwd_unavailable:
        return "cwd_unavailable";
    case process_error::environment_failed:
        return "environment_failed";
    case process_error::spawn_failed:
        return "spawn_failed";
    case process_error::wait_failed:
        return "wait_failed";
    case process_error::encoding_failed:
        return "encoding_failed";
    case process_error::unsupported:
        return "unsupported";
    }
    return "spawn_failed";
}

struct ProcessSpec {
    std::string program;
    std::vector<std::string> args;
    std::filesystem::path cwd;
    std::optional<std::chrono::milliseconds> timeout = std::nullopt;
    std::map<std::string, std::string> env_overrides;
};

struct ProcessResult {
    int exit_code = 0;
    bool timed_out = false;
};

struct CapturedProcessResult {
    int exit_code = 0;
    bool timed_out = false;
    std::string stdout_text;
    std::string stderr_text;
};

struct ProcessStreamSpec : ProcessSpec {
    std::size_t output_limit = 5 * 1024 * 1024;
};

enum class ProcessEventKind {
    stdout_chunk,
    stderr_chunk,
    exited,
    failed,
};

inline constexpr auto to_string(ProcessEventKind kind) -> std::string_view {
    switch (kind) {
    case ProcessEventKind::stdout_chunk:
        return "stdout_chunk";
    case ProcessEventKind::stderr_chunk:
        return "stderr_chunk";
    case ProcessEventKind::exited:
        return "exited";
    case ProcessEventKind::failed:
        return "failed";
    }
    return "failed";
}

struct ProcessEvent {
    ProcessEventKind kind = ProcessEventKind::stdout_chunk;
    std::string text;
    int exit_code = 0;
    bool timed_out = false;
    std::optional<process_error> error;
};

inline constexpr auto is_output(ProcessEventKind kind) -> bool {
    return kind == ProcessEventKind::stdout_chunk ||
           kind == ProcessEventKind::stderr_chunk;
}

inline constexpr auto is_terminal(ProcessEventKind kind) -> bool {
    return kind == ProcessEventKind::exited ||
           kind == ProcessEventKind::failed;
}

struct ProcessEventSummary {
    int exit_code = 0;
    bool timed_out = false;
    bool exited = false;
    std::optional<process_error> error;
    std::string stdout_text;
    std::string stderr_text;

    bool ok() const {
        return exited && !error && exit_code == 0 && !timed_out;
    }

    std::optional<ProcessResult> result() const {
        if (!exited || error)
            return std::nullopt;
        return ProcessResult{
            .exit_code = exit_code,
            .timed_out = timed_out,
        };
    }
};

class ProcessEventCollector {
public:
    void observe(ProcessEvent const& event) {
        switch (event.kind) {
        case ProcessEventKind::stdout_chunk:
            summary_.stdout_text += event.text;
            break;
        case ProcessEventKind::stderr_chunk:
            summary_.stderr_text += event.text;
            break;
        case ProcessEventKind::exited:
            summary_.exit_code = event.exit_code;
            summary_.timed_out = event.timed_out;
            summary_.exited = true;
            summary_.error.reset();
            break;
        case ProcessEventKind::failed:
            summary_.error = event.error.value_or(process_error::spawn_failed);
            summary_.exited = false;
            break;
        }
    }

    ProcessEventSummary const& summary() const {
        return summary_;
    }

private:
    ProcessEventSummary summary_;
};

} // namespace cppx::process
