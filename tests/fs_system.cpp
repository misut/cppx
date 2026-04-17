import cppx.fs;
import cppx.fs.system;
import cppx.test;
import std;

cppx::test::context tc;

void test_write_if_changed_and_read_text() {
    auto root = std::filesystem::temp_directory_path() / std::format(
        "cppx-fs-system-{}",
        std::chrono::steady_clock::now().time_since_epoch().count());
    auto file = root / "nested" / "sample.txt";

    auto first = cppx::fs::system::write_if_changed({
        .path = file,
        .content = "line1\nline2\n",
    });
    tc.check(first.has_value() && *first, "write_if_changed writes new file");

    auto content = cppx::fs::system::read_text(file);
    tc.check(content.has_value() && *content == "line1\nline2\n",
             "read_text preserves newlines");

    auto second = cppx::fs::system::write_if_changed({
        .path = file,
        .content = "line1\nline2\n",
    });
    tc.check(second.has_value() && !*second,
             "write_if_changed skips unchanged content");

    std::filesystem::remove_all(root);
}

void test_apply_writes() {
    auto root = std::filesystem::temp_directory_path() / std::format(
        "cppx-fs-system-batch-{}",
        std::chrono::steady_clock::now().time_since_epoch().count());

    auto result = cppx::fs::system::apply_writes({
        {.path = root / "a.txt", .content = "a\n"},
        {.path = root / "sub" / "b.txt", .content = "b\n"},
    });
    tc.check(result.has_value() && *result, "apply_writes writes multiple files");

    auto a = cppx::fs::system::read_text(root / "a.txt");
    auto b = cppx::fs::system::read_text(root / "sub" / "b.txt");
    tc.check(a.has_value() && *a == "a\n", "apply_writes writes first file");
    tc.check(b.has_value() && *b == "b\n", "apply_writes writes second file");

    std::filesystem::remove_all(root);
}

int main() {
    test_write_if_changed_and_read_text();
    test_apply_writes();
    return tc.summary("cppx.fs.system");
}
