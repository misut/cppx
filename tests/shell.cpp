import cppx.shell;
import cppx.test;
import std;

cppx::test::context tc;

void test_shell_quotes_are_explicit() {
    tc.check_eq(cppx::shell::quote_posix("a'b"),
                std::string{"'a'\\''b'"},
                "posix quote escapes single quote");
    tc.check_eq(cppx::shell::quote_powershell("a'b"),
                std::string{"'a''b'"},
                "powershell quote doubles single quote");
    tc.check_eq(cppx::shell::quote_cmd("a\"b"),
                std::string{"\"a\"\"b\""},
                "cmd quote doubles double quote");
}

void test_shell_command_builders() {
    auto posix = cppx::shell::command(cppx::shell::ShellKind::posix_sh, "echo ok");
    tc.check_eq(posix.program, std::string{"sh"}, "posix shell program");
    tc.check_eq(posix.args.at(0), std::string{"-lc"}, "posix shell flag");

    auto powershell = cppx::shell::command(
        cppx::shell::ShellKind::powershell,
        "Write-Output ok");
    tc.check_eq(powershell.program, std::string{"powershell"},
                "powershell program");
    tc.check(std::ranges::contains(powershell.args, std::string{"-NoProfile"}),
             "powershell command disables profile");
}

int main() {
    test_shell_quotes_are_explicit();
    test_shell_command_builders();
    return tc.summary("cppx.shell");
}
