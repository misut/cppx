#include <cstdlib>

import cppx.process;
import cppx.process.system;
import cppx.test;
import std;

cppx::test::context tc;

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
    test_run_preserves_exit_code();
    test_run_honors_cwd();
    test_run_applies_env_overrides();
    test_run_timeout();
#if !defined(_WIN32)
    test_run_normalizes_signal_exit();
#endif
    return tc.summary("cppx.process.system");
}
