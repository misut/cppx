import cppx.env;
import std;

// `setenv` / `_putenv_s` are POSIX/Win32 extensions, not part of `import std;`.
// Forward-declare so the test can set env vars without pulling in headers.
#if defined(_WIN32)
extern "C" int _putenv_s(char const*, char const*);
#else
extern "C" int setenv(char const*, char const*, int);
#endif

int failed = 0;

void check(bool cond, std::string_view msg) {
    if (!cond) {
        std::println(std::cerr, "FAIL: {}", msg);
        ++failed;
    }
}

void test_constants() {
#if defined(_WIN32)
    static_assert(cppx::env::PATH_SEPARATOR == ';');
    static_assert(cppx::env::EXE_SUFFIX == ".exe");
#else
    static_assert(cppx::env::PATH_SEPARATOR == ':');
    static_assert(cppx::env::EXE_SUFFIX == "");
#endif
    check(true, "compile-time constants");
}

void test_get() {
    // Unset variable returns nullopt. Use a name unlikely to be in the env.
    auto missing = cppx::env::get("CPPX_TEST_DEFINITELY_UNSET_42");
    check(!missing.has_value(), "unset env var is nullopt");

    // Set a variable and read it back.
#if defined(_WIN32)
    ::_putenv_s("CPPX_TEST_VAR", "hello");
#else
    ::setenv("CPPX_TEST_VAR", "hello", 1);
#endif
    auto present = cppx::env::get("CPPX_TEST_VAR");
    check(present.has_value() && *present == "hello", "set env var read-back");

    // Empty string is treated as "not provided".
#if defined(_WIN32)
    ::_putenv_s("CPPX_TEST_EMPTY", "");
#else
    ::setenv("CPPX_TEST_EMPTY", "", 1);
#endif
    auto empty = cppx::env::get("CPPX_TEST_EMPTY");
    check(!empty.has_value(), "empty env var treated as nullopt");
}

void test_home_dir() {
    // HOME is set in every reasonable dev/CI environment.
    auto h = cppx::env::home_dir();
    check(h.has_value(), "home_dir returns a value");
    if (h) {
        // The returned path should be absolute (or at least non-empty).
        check(!h->empty(), "home_dir is non-empty");
    }
}

void test_find_in_path() {
    // `cmake` is installed via intron in the mise.toml and is on PATH in CI.
    auto cmake = cppx::env::find_in_path("cmake");
    check(cmake.has_value(), "find_in_path finds cmake");
    if (cmake) {
        std::error_code ec;
        check(std::filesystem::is_regular_file(*cmake, ec),
              "find_in_path result is a regular file");
    }

    // Nonsense name should not resolve.
    auto nope = cppx::env::find_in_path("cppx_definitely_not_a_real_binary_zzz");
    check(!nope.has_value(), "find_in_path returns nullopt for missing binary");
}

void test_shell_quote() {
    check(cppx::env::shell_quote("foo") == "foo", "shell_quote no whitespace");
    check(cppx::env::shell_quote("") == "", "shell_quote empty");
    check(cppx::env::shell_quote("hello world") == "\"hello world\"",
          "shell_quote with space");
    check(cppx::env::shell_quote("tab\there") == "\"tab\there\"",
          "shell_quote with tab");
}

int main() {
    test_constants();
    test_get();
    test_home_dir();
    test_find_in_path();
    test_shell_quote();

    if (failed > 0) {
        std::println(std::cerr, "\n{} test(s) failed", failed);
        return 1;
    }
    std::println("all cppx.env tests passed");
    return 0;
}
