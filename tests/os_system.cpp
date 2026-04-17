#if defined(__linux__)
#include <cstdlib>
#endif

import cppx.env.system;
import cppx.os;
import cppx.os.system;
import cppx.test;
import std;

cppx::test::context tc;

void test_open_url_invalid_target() {
    auto result = cppx::os::system::open_url("README.md");
    tc.check(!result
                 && result.error() == cppx::os::open_error::invalid_target,
             "open_url rejects filesystem paths");
}

#if defined(__linux__)
struct scoped_env {
    std::string name;
    std::optional<std::string> previous;

    explicit scoped_env(std::string_view key)
        : name(key), previous(cppx::env::system::get(key)) {}

    ~scoped_env() {
        if (previous) {
            ::setenv(name.c_str(), previous->c_str(), 1);
        } else {
            ::unsetenv(name.c_str());
        }
    }
};

void test_open_url_missing_backend() {
    scoped_env restore_path{"PATH"};
    ::setenv("PATH", "/cppx/definitely-missing", 1);

    auto result = cppx::os::system::open_url("https://example.com");
    tc.check(!result
                 && result.error() == cppx::os::open_error::backend_unavailable,
             "open_url reports missing xdg-open");
}
#endif

int main() {
    test_open_url_invalid_target();
#if defined(__linux__)
    test_open_url_missing_backend();
#endif
    return tc.summary("cppx.os.system");
}
