#include <cstdlib>

import cppx.process;
import cppx.process.system;
import cppx.test;
import std;

cppx::test::context tc;

void test_empty_program_is_rejected() {
    auto run_result = cppx::process::system::run({});
    tc.check(
        !run_result &&
            run_result.error() == cppx::process::process_error::empty_program,
        "run rejects empty program");

    auto capture_result = cppx::process::system::capture({});
    tc.check(
        !capture_result &&
            capture_result.error() == cppx::process::process_error::empty_program,
        "capture rejects empty program");
}

void test_run_preserves_exit_code() {
#if defined(_WIN32)
    auto result = cppx::process::system::run({
        .program = "cmd",
        .args = {"/c", "exit 7"},
    });
#else
    auto result = cppx::process::system::run({
        .program = "sh",
        .args = {"-c", "exit 7"},
    });
#endif

    tc.check(result.has_value() && result->exit_code == 7 && !result->timed_out,
             "run preserves child exit code");
}

void test_run_honors_cwd() {
    auto root = std::filesystem::temp_directory_path() / std::format(
        "cppx-process-system-{}",
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root);
    {
        std::ofstream out(root / "marker.txt");
        out << "ok";
    }

#if defined(_WIN32)
    auto result = cppx::process::system::run({
        .program = "cmd",
        .args = {"/c", "if exist marker.txt (exit 0) else (exit 1)"},
        .cwd = root,
    });
#else
    auto result = cppx::process::system::run({
        .program = "sh",
        .args = {"-c", "test -f marker.txt"},
        .cwd = root,
    });
#endif

    tc.check(result.has_value() && result->exit_code == 0,
             "run honors cwd");
    std::filesystem::remove_all(root);
}

void test_run_applies_env_overrides() {
#if defined(_WIN32)
    auto result = cppx::process::system::run({
        .program = "cmd",
        .args = {"/c", "if \"%CPPX_PROCESS_TEST%\"==\"ok\" (exit 0) else (exit 1)"},
        .env_overrides = {{"CPPX_PROCESS_TEST", "ok"}},
    });
#else
    auto result = cppx::process::system::run({
        .program = "sh",
        .args = {"-c", "test \"$CPPX_PROCESS_TEST\" = ok"},
        .env_overrides = {{"CPPX_PROCESS_TEST", "ok"}},
    });
#endif

    tc.check(result.has_value() && result->exit_code == 0,
             "run applies env overrides");
}

void test_run_timeout() {
#if defined(_WIN32)
    auto result = cppx::process::system::run({
        .program = "powershell",
        .args = {"-NoProfile", "-Command", "Start-Sleep -Seconds 2"},
        .timeout = std::chrono::milliseconds{100},
    });
#else
    auto result = cppx::process::system::run({
        .program = "sh",
        .args = {"-c", "sleep 2"},
        .timeout = std::chrono::milliseconds{100},
    });
#endif

    tc.check(result.has_value() && result->timed_out && result->exit_code == 124,
             "run returns timeout result");
}

void test_capture_collects_stdout_stderr_and_exit_code() {
#if defined(_WIN32)
    auto result = cppx::process::system::capture({
        .program = "cmd",
        .args = {"/c", "(echo stdout-line)&(echo stderr-line 1>&2)&exit /b 9"},
    });
#else
    auto result = cppx::process::system::capture({
        .program = "sh",
        .args = {"-c", "printf 'stdout-line\\n'; printf 'stderr-line\\n' >&2; exit 9"},
    });
#endif

    tc.check(result.has_value(), "capture succeeds");
    if (!result)
        return;

    tc.check(result->exit_code == 9 && !result->timed_out,
             "capture preserves exit code");
    tc.check(result->stdout_text.contains("stdout-line"),
             "capture collects stdout");
    tc.check(result->stderr_text.contains("stderr-line"),
             "capture collects stderr");
}

void test_capture_timeout() {
#if defined(_WIN32)
    auto result = cppx::process::system::capture({
        .program = "powershell",
        .args = {"-NoProfile", "-Command", "Write-Output before; Start-Sleep -Seconds 2"},
        .timeout = std::chrono::milliseconds{100},
    });
#else
    auto result = cppx::process::system::capture({
        .program = "sh",
        .args = {"-c", "printf before; sleep 2"},
        .timeout = std::chrono::milliseconds{100},
    });
#endif

    tc.check(result.has_value(), "capture timeout result returned");
    if (!result)
        return;

    tc.check(result->timed_out && result->exit_code == 124,
             "capture returns timeout exit code");
}

void test_spawn_streams_captured_output() {
    auto spec = cppx::process::ProcessStreamSpec{};
#if defined(_WIN32)
    spec.program = "cmd";
    spec.args = {"/c", "(echo stream-out)&(echo stream-err 1>&2)&exit /b 5"};
#else
    spec.program = "sh";
    spec.args = {"-c", "printf stream-out; printf stream-err >&2; exit 5"};
#endif

    auto child = cppx::process::system::spawn(std::move(spec));
    tc.check(child.has_value(), "spawn returns child process");
    if (!child)
        return;

    auto stdout_seen = false;
    auto stderr_seen = false;
    auto exited = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (std::chrono::steady_clock::now() < deadline) {
        auto event = child->wait_event();
        if (!event)
            break;
        if (event->kind == cppx::process::ProcessEventKind::stdout_chunk)
            stdout_seen = event->text.contains("stream-out");
        if (event->kind == cppx::process::ProcessEventKind::stderr_chunk)
            stderr_seen = event->text.contains("stream-err");
        if (event->kind == cppx::process::ProcessEventKind::exited) {
            exited = true;
            tc.check_eq(event->exit_code, 5, "spawn preserves exit code");
            break;
        }
    }

    tc.check(stdout_seen, "spawn emits stdout chunk");
    tc.check(stderr_seen, "spawn emits stderr chunk");
    tc.check(exited, "spawn emits exit event");
}

#if !defined(_WIN32)
void test_run_normalizes_signal_exit() {
    auto result = cppx::process::system::run({
        .program = "sh",
        .args = {"-c", "kill -TERM $$"},
    });

    tc.check(result.has_value() && result->exit_code == 143,
             "run normalizes Unix signal exit");
}
#endif

int main() {
    test_empty_program_is_rejected();
    test_run_preserves_exit_code();
    test_run_honors_cwd();
    test_run_applies_env_overrides();
    test_run_timeout();
    test_capture_collects_stdout_stderr_and_exit_code();
    test_capture_timeout();
    test_spawn_streams_captured_output();
#if !defined(_WIN32)
    test_run_normalizes_signal_exit();
#endif
    return tc.summary("cppx.process.system");
}
