#include <cstdlib>

import cppx.terminal;
import cppx.terminal.system;
import cppx.test;
import std;

cppx::test::context tc;

struct scoped_env_var {
    std::string name;
    std::optional<std::string> previous;

    explicit scoped_env_var(std::string_view key)
        : name(key) {
        auto owned = std::string{key};
        if (auto* value = std::getenv(owned.c_str()); value)
            previous = std::string{value};
    }

    ~scoped_env_var() {
#if defined(_WIN32)
        if (previous)
            _putenv_s(name.c_str(), previous->c_str());
        else
            _putenv_s(name.c_str(), "");
#else
        if (previous)
            ::setenv(name.c_str(), previous->c_str(), 1);
        else
            ::unsetenv(name.c_str());
#endif
    }
};

void set_env_var(std::string_view name, std::string_view value) {
#if defined(_WIN32)
    _putenv_s(std::string{name}.c_str(), std::string{value}.c_str());
#else
    ::setenv(std::string{name}.c_str(), std::string{value}.c_str(), 1);
#endif
}

void clear_env_var(std::string_view name) {
#if defined(_WIN32)
    _putenv_s(std::string{name}.c_str(), "");
#else
    ::unsetenv(std::string{name}.c_str());
#endif
}

void test_env_capability_setting() {
    auto restore = scoped_env_var{"CPPX_TERMINAL_TEST_COLOR"};
    clear_env_var("CPPX_TERMINAL_TEST_COLOR");
    tc.check(!cppx::terminal::system::env_capability_setting(
                 "CPPX_TERMINAL_TEST_COLOR").has_value(),
             "missing capability env returns nullopt");

    set_env_var("CPPX_TERMINAL_TEST_COLOR", "never");
    auto parsed = cppx::terminal::system::env_capability_setting(
        "CPPX_TERMINAL_TEST_COLOR");
    tc.check(parsed == cppx::terminal::CapabilitySetting::never,
             "capability env parses value");

    set_env_var("CPPX_TERMINAL_TEST_COLOR", "bogus");
    tc.check(!cppx::terminal::system::env_capability_setting(
                 "CPPX_TERMINAL_TEST_COLOR").has_value(),
             "invalid capability env is ignored");
}

void test_resolve_capability_precedence() {
    auto restore = scoped_env_var{"CPPX_TERMINAL_TEST_COLOR"};
    set_env_var("CPPX_TERMINAL_TEST_COLOR", "never");
    auto resolved = cppx::terminal::system::resolve_capability(
        cppx::terminal::CapabilitySetting::always,
        "CPPX_TERMINAL_TEST_COLOR");
    tc.check(resolved == cppx::terminal::CapabilitySetting::always,
             "explicit setting wins over env");

    resolved = cppx::terminal::system::resolve_capability(
        std::nullopt,
        "CPPX_TERMINAL_TEST_COLOR");
    tc.check(resolved == cppx::terminal::CapabilitySetting::never,
             "env setting applies when explicit setting is absent");
}

void test_no_color_requested() {
    auto restore = scoped_env_var{"NO_COLOR"};
    clear_env_var("NO_COLOR");
    tc.check(!cppx::terminal::system::no_color_requested(),
             "NO_COLOR missing means color is not globally disabled");

    set_env_var("NO_COLOR", "1");
    tc.check(cppx::terminal::system::no_color_requested(),
             "NO_COLOR non-empty disables auto color");
}

void test_force_color_requested() {
    auto restore = scoped_env_var{"FORCE_COLOR"};
    clear_env_var("FORCE_COLOR");
    tc.check(!cppx::terminal::system::force_color_requested(),
             "FORCE_COLOR missing does not force color");

    set_env_var("FORCE_COLOR", "0");
    tc.check(!cppx::terminal::system::force_color_requested(),
             "FORCE_COLOR=0 does not force color");

    set_env_var("FORCE_COLOR", "1");
    tc.check(cppx::terminal::system::force_color_requested(),
             "FORCE_COLOR non-empty and non-zero forces color");
}

void test_explicit_color_always() {
    auto options = cppx::terminal::TerminalOptions{
        .color = cppx::terminal::CapabilitySetting::always,
    };
    tc.check(cppx::terminal::system::stdout_color_enabled(options),
             "explicit color always enables stdout color");
    tc.check(cppx::terminal::system::stderr_color_enabled(options),
             "explicit color always enables stderr color");
}

void test_force_color_auto_mode() {
    auto no_color = scoped_env_var{"NO_COLOR"};
    auto force_color = scoped_env_var{"FORCE_COLOR"};
    clear_env_var("NO_COLOR");
    set_env_var("FORCE_COLOR", "1");

    tc.check(cppx::terminal::system::stdout_color_enabled({}),
             "FORCE_COLOR enables stdout color in auto mode");

    set_env_var("NO_COLOR", "1");
    tc.check(!cppx::terminal::system::stdout_color_enabled({}),
             "NO_COLOR still wins over FORCE_COLOR in auto mode");
}

void test_progress_allowed_flag() {
    auto options = cppx::terminal::TerminalOptions{
        .progress = cppx::terminal::CapabilitySetting::always,
        .progress_allowed = false,
    };
    tc.check(!cppx::terminal::system::stdout_progress_enabled(options),
             "progress_allowed=false disables progress before terminal checks");
}

void test_stdout_capabilities_shape() {
    auto capabilities = cppx::terminal::system::stdout_capabilities({});
    tc.check(capabilities.is_terminal ==
                 cppx::terminal::system::stdout_is_terminal(),
             "stdout capabilities report terminal detection");
    tc.check(capabilities.color_enabled ==
                 cppx::terminal::system::stdout_color_enabled({}),
             "stdout capabilities report color detection");
}

int main() {
    test_env_capability_setting();
    test_resolve_capability_precedence();
    test_no_color_requested();
    test_force_color_requested();
    test_explicit_color_always();
    test_force_color_auto_mode();
    test_progress_allowed_flag();
    test_stdout_capabilities_shape();
    return tc.summary("cppx.terminal.system");
}
