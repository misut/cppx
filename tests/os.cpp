import cppx.os;
import cppx.test;
import std;

cppx::test::context tc;

void test_to_string() {
    tc.check(cppx::os::to_string(cppx::os::open_error::invalid_target)
                 == "invalid_target",
             "to_string invalid_target");
    tc.check(cppx::os::to_string(cppx::os::open_error::unsupported)
                 == "unsupported",
             "to_string unsupported");
    tc.check(cppx::os::to_string(cppx::os::open_error::backend_unavailable)
                 == "backend_unavailable",
             "to_string backend_unavailable");
    tc.check(cppx::os::to_string(cppx::os::open_error::open_failed)
                 == "open_failed",
             "to_string open_failed");
}

int main() {
    test_to_string();
    return tc.summary("cppx.os");
}
