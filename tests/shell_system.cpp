import cppx.shell;
import cppx.shell.system;
import cppx.test;
import std;

cppx::test::context tc;

std::string echo_script() {
#if defined(_WIN32)
    return "Write-Output shell-ok";
#else
    return "printf shell-ok";
#endif
}

void test_foreground_shell_command() {
    auto result = cppx::shell::system::run_foreground(echo_script());
    tc.check(result.has_value(), "foreground shell command succeeds");
    if (!result)
        return;
    tc.check_eq(result->exit_code, 0, "foreground exit code");
    tc.check(result->output.contains("shell-ok"), "foreground captures output");
}

void test_execution_policy_can_deny() {
    auto result = cppx::shell::system::run_foreground(
        echo_script(),
        {.policy = {
            .allow = [](std::string_view) { return false; },
            .denial_message = "nope",
        }});
    tc.check(!result, "denied command returns error");
    if (!result)
        tc.check_eq(result.error(), std::string{"nope"},
                    "denial message is surfaced");
}

void test_background_job_snapshot() {
    auto jobs = cppx::shell::system::JobRegistry{};
    auto id = jobs.start(echo_script());

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    auto snap = std::optional<cppx::shell::system::ShellJobSnapshot>{};
    while (std::chrono::steady_clock::now() < deadline) {
        snap = jobs.snapshot(id);
        if (snap && snap->state == cppx::shell::system::ShellJobState::exited)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }

    tc.check(snap.has_value(), "background job has snapshot");
    if (!snap)
        return;
    tc.check(snap->state == cppx::shell::system::ShellJobState::exited,
             "background job exits");
    tc.check(snap->recent_output.contains("shell-ok"),
             "background job keeps recent output");
}

int main() {
    test_foreground_shell_command();
    test_execution_policy_can_deny();
    test_background_job_snapshot();
    return tc.summary("cppx.shell.system");
}
