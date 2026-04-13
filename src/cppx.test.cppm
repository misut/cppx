// Minimal test utilities. Each test file keeps its own main() and
// remains a standalone executable — this module just eliminates the
// copy-pasted check/summary boilerplate.

export module cppx.test;
import std;

export namespace cppx::test {

class context {
public:
    void check(bool cond, std::string_view msg) {
        ++total_;
        if (!cond) {
            std::println(std::cerr, "  FAIL: {}", msg);
            ++failed_;
        }
    }

    template <class A, class B>
    void check_eq(A const& a, B const& b, std::string_view msg) {
        ++total_;
        if (a != b) {
            std::println(std::cerr, "  FAIL: {} — got '{}', expected '{}'",
                         msg, a, b);
            ++failed_;
        }
    }

    template <class T, class E>
    auto check_expected(std::expected<T, E> const& val, std::string_view msg)
        -> std::optional<std::reference_wrapper<T const>> {
        ++total_;
        if (!val.has_value()) {
            std::println(std::cerr, "  FAIL: {} — unexpected error", msg);
            ++failed_;
            return std::nullopt;
        }
        return std::cref(val.value());
    }

    // Returns exit code: 0 on success, 1 on failure.
    // Prints a one-line summary to stdout or stderr.
    auto summary(std::string_view suite = {}) const -> int {
        if (failed_ > 0) {
            std::println(std::cerr, "\n{} of {} test(s) failed", failed_, total_);
            return 1;
        }
        if (suite.empty())
            std::println("all {} test(s) passed", total_);
        else
            std::println("all {} {} test(s) passed", total_, suite);
        return 0;
    }

    auto failed() const -> int { return failed_; }
    auto total() const -> int { return total_; }

private:
    int failed_ = 0;
    int total_  = 0;
};

} // namespace cppx::test
