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

} // namespace cppx::process
