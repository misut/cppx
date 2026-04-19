import cppx.bytes;
import cppx.fs;
import cppx.fs.system;
import cppx.test;
import std;

cppx::test::context tc;

auto same_bytes(cppx::bytes::bytes_view lhs, cppx::bytes::bytes_view rhs) -> bool {
    if (lhs.size() != rhs.size())
        return false;
    for (auto i = std::size_t{0}; i < lhs.size(); ++i) {
        if (lhs.data()[i] != rhs.data()[i])
            return false;
    }
    return true;
}

auto test_bytes(std::initializer_list<unsigned int> values)
    -> cppx::bytes::byte_buffer {
    auto raw = std::vector<std::byte>{};
    raw.reserve(values.size());
    for (auto value : values)
        raw.push_back(static_cast<std::byte>(value));
    return cppx::bytes::byte_buffer{
        cppx::bytes::bytes_view{raw.data(), raw.size()}};
}

auto text_bytes(std::string_view text) -> cppx::bytes::byte_buffer {
    return cppx::bytes::byte_buffer{
        cppx::bytes::bytes_view{
            std::as_bytes(std::span{text.data(), text.size()})}};
}

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

void test_binary_roundtrip() {
    auto root = std::filesystem::temp_directory_path() / std::format(
        "cppx-fs-system-bytes-{}",
        std::chrono::steady_clock::now().time_since_epoch().count());
    auto file = root / "payload.bin";
    auto payload = test_bytes({0x00, 0x41, 0xFF, 0x10, 0x7E});

    auto write = cppx::fs::system::write_bytes(file, payload.view());
    tc.check(write.has_value(), "write_bytes writes binary data");

    auto read = cppx::fs::system::read_bytes(file);
    tc.check(read.has_value(), "read_bytes reads binary data");
    if (read) {
        tc.check(same_bytes(read->view(), payload.view()),
                 "binary roundtrip preserves bytes");
    }

    std::filesystem::remove_all(root);
}

void test_append_bytes() {
    auto root = std::filesystem::temp_directory_path() / std::format(
        "cppx-fs-system-append-{}",
        std::chrono::steady_clock::now().time_since_epoch().count());
    auto file = root / "append.bin";
    auto first = text_bytes("hello");
    auto second = text_bytes(" world");

    auto write = cppx::fs::system::write_bytes(file, first.view());
    tc.check(write.has_value(), "write_bytes creates append target");

    auto append = cppx::fs::system::append_bytes(file, second.view());
    tc.check(append.has_value(), "append_bytes appends data");

    auto read = cppx::fs::system::read_bytes(file);
    tc.check(read.has_value(), "read_bytes reads appended file");
    if (read) {
        auto text = std::string{
            reinterpret_cast<char const*>(read->data()),
            read->size()};
        tc.check(text == "hello world", "append_bytes preserves order");
    }

    std::filesystem::remove_all(root);
}

void test_bytes_path_with_spaces() {
    auto root = std::filesystem::temp_directory_path() / std::format(
        "cppx-fs-system spaces {}",
        std::chrono::steady_clock::now().time_since_epoch().count());
    auto file = root / "dir with spaces" / "file name.bin";
    auto payload = text_bytes("spaced");

    auto write = cppx::fs::system::write_bytes(file, payload.view());
    tc.check(write.has_value(), "write_bytes handles path with spaces");

    auto read = cppx::fs::system::read_bytes(file);
    tc.check(read.has_value(), "read_bytes handles path with spaces");
    if (read)
        tc.check(same_bytes(read->view(), payload.view()),
                 "path with spaces preserves bytes");

    std::filesystem::remove_all(root);
}

int main() {
    test_write_if_changed_and_read_text();
    test_apply_writes();
    test_binary_roundtrip();
    test_append_bytes();
    test_bytes_path_with_spaces();
    return tc.summary("cppx.fs.system");
}
