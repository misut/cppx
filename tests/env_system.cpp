// Smoke test for the impure cppx.env.system layer. The pure logic
// is covered by tests/env.cpp against fakes — this test only
// confirms the std::getenv / std::filesystem plumbing is wired up.
// It is the only test in cppx that touches the real environment.

import cppx.env;
import cppx.env.system;
import cppx.test;
import std;

cppx::test::context tc;

int main() {
    // HOME (or USERPROFILE on Windows) is set in every reasonable
    // dev/CI environment.
    auto const h = cppx::env::system::home_dir();
    tc.check(h.has_value(), "system::home_dir returns a value");
    if (h)
        tc.check(!h->empty(), "system::home_dir is non-empty");

    // `cmake` is installed via intron and on PATH in CI.
    auto const cmake = cppx::env::system::find_in_path("cmake");
    tc.check(cmake.has_value(), "system::find_in_path finds cmake");
    if (!cmake) {
        std::println(std::cerr,
                     "  hint: did you `eval \"$(intron env)\"`?");
    }

    // Nonsense name should resolve to not_found_on_PATH (NOT
    // no_PATH_set, since PATH is set in any sane shell).
    auto const nope = cppx::env::system::find_in_path(
        "cppx_definitely_not_a_real_binary_zzz");
    tc.check(!nope.has_value() &&
              nope.error() ==
                  cppx::env::find_error::not_found_on_PATH,
          "system::find_in_path missing → not_found_on_PATH");

    return tc.summary("cppx.env.system");
}
