import cppx.test;
import cppx.unicode;
import std;

cppx::test::context tc;

namespace {

constexpr std::string_view sample_utf8 =
    "Hello "
    "\xEC\x95\x88\xEB\x85\x95"
    " "
    "\xF0\x9F\x99\x82";

constexpr std::string_view ascii_text = "hello";
constexpr std::string_view korean_text =
    "\xEA\xB0\x80"
    "\xEB\x82\x98"
    "\xEB\x8B\xA4";
constexpr std::string_view emoji_text =
    "A"
    "\xF0\x9F\x99\x82"
    "B";
constexpr std::string_view mixed_text =
    "A"
    "\xEC\xB0\xAC"
    "\xF0\x9F\x99\x82"
    "B";

auto utf8_boundaries(std::string_view text) -> std::vector<std::size_t> {
    auto boundaries = std::vector<std::size_t>{0};
    std::size_t pos = 0;
    while (pos < text.size()) {
        pos = cppx::unicode::next_utf8_boundary(text, pos);
        boundaries.push_back(pos);
    }
    return boundaries;
}

void check_range_eq(
        cppx::unicode::utf8_range actual,
        cppx::unicode::utf8_range expected,
        std::string_view label) {
    tc.check_eq(actual.start, expected.start,
                std::format("{} start", label));
    tc.check_eq(actual.end, expected.end,
                std::format("{} end", label));
}

void check_utf16_roundtrip(std::string_view text, std::string_view label) {
    auto const boundaries = utf8_boundaries(text);

    for (auto boundary : boundaries) {
        auto const units = cppx::unicode::utf16_length(text.substr(0, boundary));
        tc.check_eq(
            cppx::unicode::utf16_offset_to_utf8(text, units),
            boundary,
            std::format("{} boundary round-trip {}", label, boundary));
    }

    for (std::size_t i = 0; i < boundaries.size(); ++i) {
        for (std::size_t j = i; j < boundaries.size(); ++j) {
            auto const start = boundaries[i];
            auto const end = boundaries[j];
            auto const location =
                cppx::unicode::utf16_length(text.substr(0, start));
            auto const length =
                cppx::unicode::utf16_length(text.substr(start, end - start));
            check_range_eq(
                cppx::unicode::utf16_range_to_utf8(text, location, length),
                {start, end},
                std::format("{} range round-trip {}..{}", label, start, end));
        }
    }
}

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

void test_utf8_boundaries_ascii() {
    tc.check_eq(cppx::unicode::clamp_utf8_boundary(ascii_text, 0), 0u,
                "ASCII clamp at start");
    tc.check_eq(cppx::unicode::clamp_utf8_boundary(ascii_text, 3), 3u,
                "ASCII clamp keeps boundary");
    tc.check_eq(cppx::unicode::clamp_utf8_boundary(ascii_text, 99), ascii_text.size(),
                "ASCII clamp out of range");

    tc.check_eq(cppx::unicode::prev_utf8_boundary(ascii_text, 3), 2u,
                "ASCII prev boundary");
    tc.check_eq(cppx::unicode::next_utf8_boundary(ascii_text, 3), 4u,
                "ASCII next boundary");
    tc.check_eq(cppx::unicode::utf16_length(ascii_text), ascii_text.size(),
                "ASCII UTF-16 length matches byte length");
}

void test_utf8_boundaries_korean() {
    tc.check_eq(cppx::unicode::clamp_utf8_boundary(korean_text, 1), 0u,
                "Korean clamp inside first syllable");
    tc.check_eq(cppx::unicode::clamp_utf8_boundary(korean_text, 4), 3u,
                "Korean clamp inside second syllable");
    tc.check_eq(cppx::unicode::prev_utf8_boundary(korean_text, 4), 0u,
                "Korean prev boundary");
    tc.check_eq(cppx::unicode::next_utf8_boundary(korean_text, 4), 6u,
                "Korean next boundary");
    tc.check_eq(cppx::unicode::utf16_length(korean_text), 3u,
                "Korean UTF-16 length counts code points");
}

void test_utf16_offsets_and_ranges() {
    tc.check_eq(cppx::unicode::utf16_length(emoji_text), 4u,
                "emoji text UTF-16 length");
    tc.check_eq(cppx::unicode::utf16_offset_to_utf8(emoji_text, 0), 0u,
                "UTF-16 offset zero");
    tc.check_eq(cppx::unicode::utf16_offset_to_utf8(emoji_text, 1), 1u,
                "UTF-16 offset after ASCII prefix");
    tc.check_eq(cppx::unicode::utf16_offset_to_utf8(emoji_text, 2), 1u,
                "UTF-16 offset clamps inside surrogate pair");
    tc.check_eq(cppx::unicode::utf16_offset_to_utf8(emoji_text, 3), 5u,
                "UTF-16 offset after surrogate pair");
    tc.check_eq(cppx::unicode::utf16_offset_to_utf8(emoji_text, 99), emoji_text.size(),
                "UTF-16 offset clamps out of range");

    check_range_eq(
        cppx::unicode::utf16_range_to_utf8(emoji_text, 1, 2),
        {1, 5},
        "emoji UTF-16 range");
    check_range_eq(
        cppx::unicode::utf16_range_to_utf8(mixed_text, 1, 3),
        {1, 8},
        "mixed UTF-16 range");
    check_range_eq(
        cppx::unicode::utf16_range_to_utf8(mixed_text, 99, 99),
        {mixed_text.size(), mixed_text.size()},
        "out-of-range UTF-16 range");
}

void test_empty_boundaries() {
    tc.check_eq(cppx::unicode::clamp_utf8_boundary("", 0), 0u,
                "empty clamp at zero");
    tc.check_eq(cppx::unicode::clamp_utf8_boundary("", 5), 0u,
                "empty clamp out of range");
    tc.check_eq(cppx::unicode::prev_utf8_boundary("", 5), 0u,
                "empty prev boundary");
    tc.check_eq(cppx::unicode::next_utf8_boundary("", 5), 0u,
                "empty next boundary");
    tc.check_eq(cppx::unicode::utf16_length(""), 0u,
                "empty UTF-16 length");
    tc.check_eq(cppx::unicode::utf16_offset_to_utf8("", 5), 0u,
                "empty UTF-16 offset");
    check_range_eq(
        cppx::unicode::utf16_range_to_utf8("", 5, 5),
        {0, 0},
        "empty UTF-16 range");
}

void test_utf16_roundtrip_checks() {
    check_utf16_roundtrip(ascii_text, "ASCII");
    check_utf16_roundtrip(korean_text, "Korean");
    check_utf16_roundtrip(sample_utf8, "mixed sample");
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
    test_utf8_boundaries_ascii();
    test_utf8_boundaries_korean();
    test_utf16_offsets_and_ranges();
    test_empty_boundaries();
    test_utf16_roundtrip_checks();
    test_wide_roundtrip();
    test_invalid_utf8();
    test_invalid_utf16();
    return tc.summary("cppx.unicode");
}
