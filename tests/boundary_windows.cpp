import cppx.boundary.windows.test;
import cppx.os;
import cppx.resource;
import cppx.test;
import std;

cppx::test::context tc;

namespace {

constexpr std::string_view sample_utf8 =
    "Hello "
    "\xEC\x95\x88\xEB\x85\x95"
    " "
    "\xF0\x9F\x99\x82";

} // namespace

void test_unicode_roundtrip() {
    auto roundtrip = cppx::boundary::windows::test::roundtrip_utf8_via_wide(sample_utf8);
    tc.check(roundtrip.has_value(), "consumer module wide roundtrip succeeds");
    if (!roundtrip)
        return;

    tc.check_eq(*roundtrip, std::string{sample_utf8},
                "consumer module preserves UTF-8 text");
}

void test_resource_helpers() {
    auto kind = cppx::boundary::windows::test::classify_target("https://example.com");
    tc.check(kind == cppx::resource::resource_kind::https_url,
             "consumer module classifies remote URL");

    auto resolved = cppx::boundary::windows::test::resolve_target(
        std::filesystem::path{"workspace/project"},
        "assets/../image.png");
    tc.check(resolved == std::filesystem::path{"workspace/project/image.png"},
             "consumer module resolves local image path");
}

void test_open_url_probe() {
    auto opened = cppx::boundary::windows::test::open_url_probe("README.md");
    tc.check(!opened
                 && opened.error() == cppx::os::open_error::invalid_target,
             "consumer module open_url probe stays on invalid-target path");
}

int main() {
    test_unicode_roundtrip();
    test_resource_helpers();
    test_open_url_probe();
    return tc.summary("cppx.boundary.windows.test");
}
