import cppx.test;
import cppx.unicode;
import std;

cppx::test::context tc;

namespace {

constexpr auto sample_utf8 =
    "Hello "
    "\xEC\x95\x88\xEB\x85\x95"
    " "
    "\xF0\x9F\x99\x82";

} // namespace

void test_utf8_to_codepoints() {
    auto cps = cppx::unicode::utf8_to_codepoints(sample_utf8);
    tc.check(cps.has_value(), "utf8_to_codepoints succeeds");
    if (!cps)
        return;

    tc.check(cps->size() == 10, "decoded code point count");
    tc.check((*cps)[6] == static_cast<char32_t>(0xC548), "first Hangul code point");
    tc.check((*cps)[7] == static_cast<char32_t>(0xB155), "second Hangul code point");
    tc.check((*cps)[9] == static_cast<char32_t>(0x1F642), "emoji code point");
}

void test_utf8_to_utf16_and_back() {
    auto utf16 = cppx::unicode::utf8_to_utf16(sample_utf8);
    tc.check(utf16.has_value(), "utf8_to_utf16 succeeds");
    if (!utf16)
        return;

    tc.check(utf16->back() == static_cast<char16_t>(0xDE42),
             "emoji encoded as surrogate pair");

    auto roundtrip = cppx::unicode::utf16_to_utf8(*utf16);
    tc.check(roundtrip.has_value() && *roundtrip == sample_utf8,
             "utf16_to_utf8 roundtrip");
}

void test_wide_roundtrip() {
    auto wide = cppx::unicode::utf8_to_wide(sample_utf8);
    tc.check(wide.has_value(), "utf8_to_wide succeeds");
    if (!wide)
        return;

    auto roundtrip = cppx::unicode::wide_to_utf8(*wide);
    tc.check(roundtrip.has_value() && *roundtrip == sample_utf8,
             "wide_to_utf8 roundtrip");
}

void test_invalid_utf8() {
    auto invalid = cppx::unicode::utf8_to_codepoints("\xF0\x28\x8C\x28");
    tc.check(!invalid
                 && invalid.error() == cppx::unicode::unicode_error::invalid_utf8,
             "invalid UTF-8 rejected");

    auto truncated = cppx::unicode::utf8_to_codepoints("\xE2\x82");
    tc.check(!truncated
                 && truncated.error()
                        == cppx::unicode::unicode_error::truncated_sequence,
             "truncated UTF-8 rejected");
}

void test_invalid_utf16() {
    auto invalid = cppx::unicode::utf16_to_utf8(
        std::u16string_view{u"\xDC00"});
    tc.check(!invalid
                 && invalid.error() == cppx::unicode::unicode_error::invalid_utf16,
             "lone low surrogate rejected");

    std::u16string truncated;
    truncated.push_back(static_cast<char16_t>(0xD83D));
    auto result = cppx::unicode::utf16_to_utf8(truncated);
    tc.check(!result
                 && result.error()
                        == cppx::unicode::unicode_error::truncated_sequence,
             "truncated UTF-16 surrogate pair rejected");
}

int main() {
    test_utf8_to_codepoints();
    test_utf8_to_utf16_and_back();
    test_wide_roundtrip();
    test_invalid_utf8();
    test_invalid_utf16();
    return tc.summary("cppx.unicode");
}
