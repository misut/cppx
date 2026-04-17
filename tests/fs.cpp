import cppx.fs;
import cppx.test;
import std;

cppx::test::context tc;

void test_text_write_shape() {
    cppx::fs::TextWrite write{
        .path = "build/output.txt",
        .content = "hello\n",
    };

    tc.check(write.path == std::filesystem::path{"build/output.txt"}, "path stored");
    tc.check(write.content == "hello\n", "content stored");
    tc.check(write.skip_if_unchanged, "skip_if_unchanged defaults to true");
}

int main() {
    test_text_write_shape();
    return tc.summary("cppx.fs");
}
