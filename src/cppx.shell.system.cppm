// System-backed shell job helpers for CLI tools. This module composes
// cppx.shell command builders with cppx.process.system execution.

export module cppx.shell.system;
import std;
import cppx.process;
import cppx.process.system;
import cppx.shell;

export namespace cppx::shell::system {

struct ExecutionPolicy {
    std::function<bool(std::string_view)> allow;
    std::string denial_message = "shell command denied by policy";

    bool allowed(std::string_view script) const {
        return !allow || allow(script);
    }
};

struct ShellJobOptions {
    cppx::shell::ShellKind shell = cppx::shell::default_shell();
    std::filesystem::path cwd;
    std::optional<std::chrono::milliseconds> timeout;
    std::size_t output_limit = 1024 * 1024;
    std::optional<std::filesystem::path> log_path;
    ExecutionPolicy policy;
};

struct ShellJobResult {
    int exit_code = 0;
    bool timed_out = false;
    std::string output;
};

enum class ShellJobState {
    running,
    exited,
    failed,
    killed,
};

struct ShellJobSnapshot {
    int id = 0;
    ShellJobState state = ShellJobState::running;
    int exit_code = 0;
    bool timed_out = false;
    std::string recent_output;
};

class JobRegistry {
public:
    int start(std::string script, ShellJobOptions options = {});
    std::optional<ShellJobSnapshot> snapshot(int id);
    std::vector<ShellJobSnapshot> snapshots();
    bool kill(int id);
    void reap_finished();

private:
    struct job {
        int id = 0;
        std::string script;
        ShellJobState state = ShellJobState::running;
        cppx::process::system::ChildProcess child;
        std::deque<std::string> output;
        std::size_t output_bytes = 0;
        std::size_t output_limit = 1024 * 1024;
        int exit_code = 0;
        bool timed_out = false;
        std::optional<std::filesystem::path> log_path;
    };

    void drain(job& item);
    void append(job& item, std::string text);

    std::map<int, job> jobs_;
    int next_id_ = 1;
};

std::expected<ShellJobResult, std::string>
run_foreground(std::string script, ShellJobOptions options = {});

} // namespace cppx::shell::system

namespace cppx::shell::system {

namespace detail {

inline cppx::process::ProcessStreamSpec stream_spec(std::string script,
                                                    ShellJobOptions const& options) {
    auto base = cppx::shell::command(options.shell, std::move(script));
    auto spec = cppx::process::ProcessStreamSpec{};
    spec.program = std::move(base.program);
    spec.args = std::move(base.args);
    spec.cwd = options.cwd;
    spec.timeout = options.timeout;
    spec.env_overrides = std::move(base.env_overrides);
    spec.output_limit = options.output_limit;
    return spec;
}

inline std::string trim_output(std::string text, std::size_t limit) {
    if (text.size() <= limit)
        return text;
    auto start = text.size() - limit;
    if (auto newline = text.find('\n', start);
        newline != std::string::npos && newline + 1 < text.size()) {
        start = newline + 1;
    }
    return std::format("...\n{}", text.substr(start));
}

} // namespace detail

std::expected<ShellJobResult, std::string>
run_foreground(std::string script, ShellJobOptions options) {
    if (!options.policy.allowed(script))
        return std::unexpected{options.policy.denial_message};

    auto spec = cppx::shell::command(options.shell, std::move(script));
    spec.cwd = options.cwd;
    spec.timeout = options.timeout;
    auto captured = cppx::process::system::capture(spec);
    if (!captured) {
        return std::unexpected{
            std::string{cppx::process::to_string(captured.error())}};
    }

    auto output = captured->stdout_text;
    output += captured->stderr_text;
    output = detail::trim_output(std::move(output), options.output_limit);
    if (options.log_path) {
        std::ofstream log{*options.log_path, std::ios::app};
        log << output;
    }

    return ShellJobResult{
        .exit_code = captured->exit_code,
        .timed_out = captured->timed_out,
        .output = std::move(output),
    };
}

int JobRegistry::start(std::string script, ShellJobOptions options) {
    auto id = next_id_++;
    auto item = job{
        .id = id,
        .script = script,
        .output_limit = options.output_limit,
        .log_path = options.log_path,
    };

    if (!options.policy.allowed(script)) {
        item.state = ShellJobState::failed;
        append(item, options.policy.denial_message);
        jobs_.emplace(id, std::move(item));
        return id;
    }

    auto child = cppx::process::system::spawn(
        detail::stream_spec(std::move(script), options));
    if (!child) {
        item.state = ShellJobState::failed;
        append(item, std::string{cppx::process::to_string(child.error())});
        jobs_.emplace(id, std::move(item));
        return id;
    }

    item.child = std::move(*child);
    jobs_.emplace(id, std::move(item));
    return id;
}

void JobRegistry::append(job& item, std::string text) {
    if (text.empty())
        return;

    item.output_bytes += text.size();
    item.output.push_back(text);
    while (item.output_bytes > item.output_limit && !item.output.empty()) {
        item.output_bytes -= item.output.front().size();
        item.output.pop_front();
    }

    if (item.log_path) {
        std::ofstream log{*item.log_path, std::ios::app};
        log << text;
    }
}

void JobRegistry::drain(job& item) {
    while (auto event = item.child.try_event()) {
        switch (event->kind) {
        case cppx::process::ProcessEventKind::stdout_chunk:
        case cppx::process::ProcessEventKind::stderr_chunk:
            append(item, std::move(event->text));
            break;
        case cppx::process::ProcessEventKind::exited:
            item.state = ShellJobState::exited;
            item.exit_code = event->exit_code;
            item.timed_out = event->timed_out;
            break;
        case cppx::process::ProcessEventKind::failed:
            item.state = ShellJobState::failed;
            if (event->error)
                append(item, std::string{cppx::process::to_string(*event->error)});
            break;
        }
    }
}

std::optional<ShellJobSnapshot> JobRegistry::snapshot(int id) {
    auto found = jobs_.find(id);
    if (found == jobs_.end())
        return std::nullopt;
    drain(found->second);

    auto output = std::string{};
    for (auto const& chunk : found->second.output)
        output += chunk;
    output = detail::trim_output(std::move(output), found->second.output_limit);

    return ShellJobSnapshot{
        .id = found->second.id,
        .state = found->second.state,
        .exit_code = found->second.exit_code,
        .timed_out = found->second.timed_out,
        .recent_output = std::move(output),
    };
}

std::vector<ShellJobSnapshot> JobRegistry::snapshots() {
    auto out = std::vector<ShellJobSnapshot>{};
    for (auto& [id, item] : jobs_) {
        (void)id;
        if (auto snap = snapshot(item.id))
            out.push_back(std::move(*snap));
    }
    return out;
}

bool JobRegistry::kill(int id) {
    auto found = jobs_.find(id);
    if (found == jobs_.end())
        return false;
    found->second.child.terminate();
    found->second.state = ShellJobState::killed;
    return true;
}

void JobRegistry::reap_finished() {
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        drain(it->second);
        if (it->second.state == ShellJobState::exited ||
            it->second.state == ShellJobState::failed ||
            it->second.state == ShellJobState::killed) {
            it = jobs_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace cppx::shell::system
