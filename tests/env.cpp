// Pure tests for cppx.env. No setenv, no real PATH, no real HOME —
// every assertion runs against in-memory fakes so the suite is
// order-independent and parallel-safe. The impure layer is smoke
// tested separately in tests/env_system.cpp.

import cppx.env;
import cppx.test;
import std;

cppx::test::context tc;

// In-memory env_source. Treats empty values as "missing" to match
// the cppx.env contract.
struct fake_env {
    std::map<std::string, std::string, std::less<>> vars;
    std::optional<std::string> get(std::string_view name) const {
        if (auto it = vars.find(name); it != vars.end() && !it->second.empty())
            return it->second;
        return std::nullopt;
    }
};

// In-memory fs_source. A path is "a regular file" iff it's in the set.
struct fake_fs {
    std::set<std::filesystem::path> files;
    bool is_regular_file(std::filesystem::path const& p) const {
        return files.contains(p);
    }
};

void test_constants() {
#if defined(_WIN32)
    static_assert(cppx::env::PATH_SEPARATOR == ';');
    static_assert(cppx::env::EXE_SUFFIX == ".exe");
#else
    static_assert(cppx::env::PATH_SEPARATOR == ':');
    static_assert(cppx::env::EXE_SUFFIX == "");
#endif
    tc.check(true, "compile-time constants");
}

void test_get() {
    auto const env = fake_env{{{"FOO", "hello"}, {"EMPTY", ""}}};
    tc.check(!cppx::env::get(env, "MISSING").has_value(),
          "get: missing → nullopt");
    auto const foo = cppx::env::get(env, "FOO");
    tc.check(foo.has_value() && *foo == "hello",
          "get: FOO → hello");
    tc.check(!cppx::env::get(env, "EMPTY").has_value(),
          "get: empty string → nullopt");
}

void test_home_dir() {
    // No HOME → nullopt.
    tc.check(!cppx::env::home_dir(cppx::env::null_env{}).has_value(),
          "home_dir: empty env → nullopt");

    // HOME set → returns it.
    auto const env = fake_env{{{"HOME", "/home/alice"}}};
    auto const h = cppx::env::home_dir(env);
    tc.check(h.has_value() && *h == std::filesystem::path{"/home/alice"},
          "home_dir: HOME → /home/alice");

#if defined(_WIN32)
    // No HOME, USERPROFILE set → falls back to USERPROFILE.
    auto const winenv = fake_env{{{"USERPROFILE", "C:\\Users\\Alice"}}};
    auto const wh = cppx::env::home_dir(winenv);
    tc.check(wh.has_value() &&
              *wh == std::filesystem::path{"C:\\Users\\Alice"},
          "home_dir: USERPROFILE fallback");
#endif
}

void test_find_in_path_no_path_set() {
    auto const r = cppx::env::find_in_path(
        cppx::env::null_env{}, cppx::env::null_fs{}, "cmake");
    tc.check(!r.has_value() &&
              r.error() == cppx::env::find_error::no_PATH_set,
          "find_in_path: no PATH → no_PATH_set");
}

void test_find_in_path_not_found() {
#if defined(_WIN32)
    auto const env = fake_env{{{"PATH", "C:\\bin;C:\\usr\\bin"}}};
#else
    auto const env = fake_env{{{"PATH", "/usr/bin:/bin"}}};
#endif
    auto const fs = fake_fs{};
    auto const r = cppx::env::find_in_path(env, fs, "ghost");
    tc.check(!r.has_value() &&
              r.error() == cppx::env::find_error::not_found_on_PATH,
          "find_in_path: missing binary → not_found_on_PATH");
}

void test_find_in_path_first_match() {
#if defined(_WIN32)
    auto const env = fake_env{{{"PATH", "C:\\bin;C:\\opt\\bin"}}};
    auto const fs = fake_fs{
        {std::filesystem::path{"C:\\opt\\bin\\cmake.exe"}}};
    auto const r = cppx::env::find_in_path(env, fs, "cmake");
    tc.check(r.has_value() &&
              *r == std::filesystem::path{"C:\\opt\\bin\\cmake.exe"},
          "find_in_path: Windows .exe suffix in second dir");
#else
    auto const env = fake_env{{{"PATH", "/usr/bin:/opt/bin"}}};
    auto const fs = fake_fs{{std::filesystem::path{"/opt/bin/cmake"}}};
    auto const r = cppx::env::find_in_path(env, fs, "cmake");
    tc.check(r.has_value() &&
              *r == std::filesystem::path{"/opt/bin/cmake"},
          "find_in_path: hit in /opt/bin");
#endif
}

void test_find_in_path_skips_empty_segments() {
    // PATH="::/opt/bin" should still find /opt/bin/tool, ignoring
    // the leading empty segments.
#if defined(_WIN32)
    auto const env = fake_env{{{"PATH", ";;C:\\opt\\bin"}}};
    auto const fs = fake_fs{
        {std::filesystem::path{"C:\\opt\\bin\\tool.exe"}}};
#else
    auto const env = fake_env{{{"PATH", "::/opt/bin"}}};
    auto const fs = fake_fs{{std::filesystem::path{"/opt/bin/tool"}}};
#endif
    auto const r = cppx::env::find_in_path(env, fs, "tool");
    tc.check(r.has_value(), "find_in_path: skips empty PATH segments");
}

void test_shell_quote() {
    tc.check(cppx::env::shell_quote("foo") == "foo", "shell_quote no whitespace");
    tc.check(cppx::env::shell_quote("") == "", "shell_quote empty");
    tc.check(cppx::env::shell_quote("hello world") == "\"hello world\"",
          "shell_quote with space");
    tc.check(cppx::env::shell_quote("tab\there") == "\"tab\there\"",
          "shell_quote with tab");
}

void test_parse_bool() {
    auto yes = cppx::env::parse_bool("YES");
    tc.check(yes.has_value() && *yes, "parse_bool YES");

    auto off = cppx::env::parse_bool("  off  ");
    tc.check(off.has_value() && !*off, "parse_bool trims whitespace");

    auto invalid = cppx::env::parse_bool("maybe");
    tc.check(!invalid
                  && invalid.error() == cppx::env::bool_parse_error::invalid_value,
              "parse_bool rejects invalid token");
}

void test_get_bool() {
    auto const env = fake_env{{
        {"ENABLED", "true"},
        {"DISABLED", "0"},
        {"INVALID", "later"},
    }};

    auto enabled = cppx::env::get_bool(env, "ENABLED");
    tc.check(enabled.has_value() && *enabled, "get_bool true");

    auto disabled = cppx::env::get_bool(env, "DISABLED");
    tc.check(disabled.has_value() && !*disabled, "get_bool false");

    tc.check(!cppx::env::get_bool(env, "INVALID").has_value(),
             "get_bool invalid token → nullopt");
    tc.check(!cppx::env::get_bool(env, "MISSING").has_value(),
             "get_bool missing → nullopt");
}

void test_get_bool_or() {
    auto const env = fake_env{{
        {"ENABLED", "on"},
        {"INVALID", "later"},
    }};

    tc.check(cppx::env::get_bool_or(env, "ENABLED", false),
             "get_bool_or returns parsed value");
    tc.check(!cppx::env::get_bool_or(env, "INVALID", false),
             "get_bool_or falls back for invalid token");
    tc.check(cppx::env::get_bool_or(env, "MISSING", true),
             "get_bool_or falls back for missing token");
}

int main() {
    test_constants();
    test_get();
    test_home_dir();
    test_find_in_path_no_path_set();
    test_find_in_path_not_found();
    test_find_in_path_first_match();
    test_find_in_path_skips_empty_segments();
    test_shell_quote();
    test_parse_bool();
    test_get_bool();
    test_get_bool_or();
    return tc.summary("cppx.env");
}
