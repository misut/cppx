// Shell command construction helpers. These are explicit shell-facing
// APIs; cppx.env::shell_quote remains only a whitespace-safe path helper.

export module cppx.shell;
import std;
import cppx.process;

export namespace cppx::shell {

enum class ShellKind {
    posix_sh,
    powershell,
    cmd,
};

inline constexpr std::string_view to_string(ShellKind kind) {
    switch (kind) {
    case ShellKind::posix_sh:
        return "posix-sh";
    case ShellKind::powershell:
        return "powershell";
    case ShellKind::cmd:
        return "cmd";
    }
    return "posix-sh";
}

inline ShellKind default_shell() {
#if defined(_WIN32)
    return ShellKind::powershell;
#else
    return ShellKind::posix_sh;
#endif
}

std::string quote_posix(std::string_view value) {
    if (value.empty())
        return "''";
    auto out = std::string{"'"};
    for (auto ch : value) {
        if (ch == '\'')
            out += "'\\''";
        else
            out.push_back(ch);
    }
    out.push_back('\'');
    return out;
}

std::string quote_powershell(std::string_view value) {
    auto out = std::string{"'"};
    for (auto ch : value) {
        if (ch == '\'')
            out += "''";
        else
            out.push_back(ch);
    }
    out.push_back('\'');
    return out;
}

std::string quote_cmd(std::string_view value) {
    if (value.empty())
        return "\"\"";
    auto out = std::string{"\""};
    for (auto ch : value) {
        if (ch == '"')
            out += "\"\"";
        else
            out.push_back(ch);
    }
    out.push_back('"');
    return out;
}

std::string quote(ShellKind kind, std::string_view value) {
    switch (kind) {
    case ShellKind::posix_sh:
        return quote_posix(value);
    case ShellKind::powershell:
        return quote_powershell(value);
    case ShellKind::cmd:
        return quote_cmd(value);
    }
    return quote_posix(value);
}

cppx::process::ProcessSpec command(ShellKind kind, std::string script) {
    switch (kind) {
    case ShellKind::posix_sh:
        return {
            .program = "sh",
            .args = {"-lc", std::move(script)},
        };
    case ShellKind::powershell:
        return {
            .program = "powershell",
            .args = {
                "-NoLogo",
                "-NoProfile",
                "-NonInteractive",
                "-ExecutionPolicy",
                "Bypass",
                "-Command",
                std::move(script),
            },
        };
    case ShellKind::cmd:
        return {
            .program = "cmd",
            .args = {"/c", std::move(script)},
        };
    }
    return {};
}

cppx::process::ProcessSpec command(std::string script) {
    return command(default_shell(), std::move(script));
}

} // namespace cppx::shell
