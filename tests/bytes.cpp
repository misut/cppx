import cppx.bytes;
import cppx.test;
import std;

cppx::test::context tc;

auto make_bytes(std::initializer_list<unsigned int> values)
    -> cppx::bytes::byte_buffer {
    auto raw = std::vector<std::byte>{};
    raw.reserve(values.size());
    for (auto value : values)
        raw.push_back(static_cast<std::byte>(value));
    return cppx::bytes::byte_buffer{
        cppx::bytes::bytes_view{raw.data(), raw.size()}};
}

void test_empty_buffer() {
    auto bytes = cppx::bytes::byte_buffer{};
    tc.check(bytes.empty(), "empty buffer reports empty");
    tc.check(bytes.size() == 0, "empty buffer size is zero");
    tc.check(bytes.view().empty(), "empty buffer view is empty");
    tc.check(bytes.subview(3).empty(), "empty buffer subview clamps to empty");
}

void test_append_behavior() {
    auto bytes = make_bytes({0x10, 0x20});
    auto extra = make_bytes({0x30, 0x40, 0x50});

    bytes.append(extra.view());

    tc.check(bytes.size() == 5, "append grows buffer");
    tc.check(bytes.subview(1, 3).size() == 3, "subview returns requested slice");
    tc.check(bytes.subview(3, 99).size() == 2, "subview clamps oversized count");
    tc.check(bytes.data()[4] == std::byte{0x50}, "append keeps byte order");
}

void test_mutable_view() {
    auto bytes = make_bytes({0x01, 0x02, 0x03});
    auto view = bytes.mutable_view();
    view.data()[1] = std::byte{0x7F};

    auto tail = view.subview(1, 8);
    tail.data()[1] = std::byte{0x55};

    tc.check(bytes.data()[1] == std::byte{0x7F}, "mutable_view writes through");
    tc.check(bytes.data()[2] == std::byte{0x55}, "mutable subview writes through");
    tc.check(view.view().subview(2).size() == 1, "bytes_view subview works");
}

int main() {
    test_empty_buffer();
    test_append_behavior();
    test_mutable_view();
    return tc.summary("cppx.bytes");
}
