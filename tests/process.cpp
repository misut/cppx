import cppx.process;
import cppx.test;
import std;

cppx::test::context tc;

void test_process_spec_shape() {
    cppx::process::ProcessSpec spec{
        .program = "cmake",
        .args = {"--build", "build"},
        .cwd = "/tmp/example",
        .timeout = std::chrono::seconds{5},
        .env_overrides = {{"CPPX_TEST", "ok"}},
    };

    tc.check(spec.program == "cmake", "program stored");
    tc.check(spec.args.size() == 2 && spec.args[1] == "build", "argv-first args stored");
    tc.check(spec.cwd == std::filesystem::path{"/tmp/example"}, "cwd stored");
    tc.check(spec.timeout == std::chrono::seconds{5}, "timeout stored");
    tc.check(spec.env_overrides.at("CPPX_TEST") == "ok", "env override stored");
}

void test_process_result_shape() {
    cppx::process::ProcessResult result{.exit_code = 124, .timed_out = true};
    tc.check(result.exit_code == 124, "exit code stored");
    tc.check(result.timed_out, "timed_out stored");
}

int main() {
    test_process_spec_shape();
    test_process_result_shape();
    return tc.summary("cppx.process");
}
