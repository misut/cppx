import cppx.pty.system;
import cppx.process;
import cppx.test;
import std;

cppx::test::context tc;

void test_pty_spawn_echo() {
#if defined(__APPLE__) || defined(__linux__)
    auto session = cppx::pty::system::spawn({
        .program = "sh",
        .args = {"-lc", "printf pty-ok"},
    });
    tc.check(session.has_value(), "pty spawn succeeds");
    if (!session)
        return;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    auto output = std::string{};
    while (std::chrono::steady_clock::now() < deadline) {
        auto event = session->read_event(std::chrono::milliseconds{50});
        if (!event)
            continue;
        if (event->kind == cppx::pty::system::PtyEventKind::output)
            output += event->text;
        if (event->kind == cppx::pty::system::PtyEventKind::exited)
            break;
    }
    tc.check(output.contains("pty-ok"), "pty captures child output");
    session->close();
#else
    auto session = cppx::pty::system::spawn({.program = "cmd"});
    tc.check(!session, "unsupported platform reports no pty");
    if (!session)
        tc.check(session.error() == cppx::process::process_error::unsupported,
                 "unsupported pty error code");
#endif
}

int main() {
    test_pty_spawn_echo();
    return tc.summary("cppx.pty.system");
}
