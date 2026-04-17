#include <cstdlib>

// Smoke test for the impure cppx.env.system layer. The pure logic
// is covered by tests/env.cpp against fakes — this test only
// confirms the std::getenv / std::filesystem plumbing is wired up.
// It is the only test in cppx that touches the real environment.

import cppx.env;
import cppx.env.system;
import cppx.test;
import std;

cppx::test::context tc;

struct scoped_env_var {
    std::string name;
    std::optional<std::string> previous;

    explicit scoped_env_var(std::string_view key)
        : name(key), previous(cppx::env::system::get(key)) {}

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

void test_find_in_path_with_injected_path() {
    auto const temp_root = std::filesystem::temp_directory_path() / std::format(
        "cppx-env-system-{}",
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(temp_root);

#if defined(_WIN32)
    auto const binary_path = temp_root / "cppx-test-tool.exe";
#else
    auto const binary_path = temp_root / "cppx-test-tool";
#endif

    {
        std::ofstream out(binary_path);
        out << "placeholder";
    }

    scoped_env_var restore_path{"PATH"};
    set_env_var("PATH", temp_root.string());

    auto const found = cppx::env::system::find_in_path("cppx-test-tool");
    tc.check(found.has_value(), "system::find_in_path finds injected PATH entry");
    if (found)
        tc.check(*found == binary_path, "system::find_in_path returns injected binary");

    std::filesystem::remove_all(temp_root);
}

int main() {
    // HOME (or USERPROFILE on Windows) is set in every reasonable
    // dev/CI environment.
    auto const h = cppx::env::system::home_dir();
    tc.check(h.has_value(), "system::home_dir returns a value");
    if (h)
        tc.check(!h->empty(), "system::home_dir is non-empty");

    test_find_in_path_with_injected_path();

    // Nonsense name should resolve to not_found_on_PATH (NOT
    // no_PATH_set, since PATH is set in any sane shell).
    auto const nope = cppx::env::system::find_in_path(
        "cppx_definitely_not_a_real_binary_zzz");
    tc.check(!nope.has_value() &&
              nope.error() ==
                  cppx::env::find_error::not_found_on_PATH,
          "system::find_in_path missing → not_found_on_PATH");

    auto const yes = cppx::env::system::parse_bool("YES");
    tc.check(yes.has_value() && *yes, "system::parse_bool parses YES");

    auto const missing = cppx::env::system::get_bool(
        "cppx_definitely_not_a_real_env_var_zzz");
    tc.check(!missing.has_value(), "system::get_bool missing → nullopt");

    return tc.summary("cppx.env.system");
}
