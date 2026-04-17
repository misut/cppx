import cppx.checksum;
import cppx.test;
import std;

cppx::test::context tc;

void test_normalize_sha256() {
    auto digest = cppx::checksum::normalize_sha256(
        "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789");
    tc.check(digest.has_value(), "normalize accepts uppercase digest");
    if (digest) {
        tc.check(*digest ==
                     "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",
                 "normalize lowercases digest");
    }

    tc.check(!cppx::checksum::normalize_sha256("not-a-digest"),
             "normalize rejects invalid digest");
}

void test_find_sha256_for_filename_supports_common_manifest_forms() {
    auto manifest = std::string{
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA  tool.tar.gz\r\n"
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB *tool.zip\r\n"
    };

    auto tar = cppx::checksum::find_sha256_for_filename(manifest, "tool.tar.gz");
    tc.check(tar.has_value(), "manifest finds double-space filename entry");
    if (tar) {
        tc.check(*tar ==
                     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                 "manifest normalizes tar digest");
    }

    auto zip = cppx::checksum::find_sha256_for_filename(manifest, "tool.zip");
    tc.check(zip.has_value(), "manifest finds star-prefixed filename entry");
    if (zip) {
        tc.check(*zip ==
                     "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                 "manifest normalizes zip digest");
    }
}

void test_find_sha256_for_filename_requires_exact_match() {
    auto manifest = std::string{
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA  tool.tar.gz.sig\n"
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB  tool.tar.gz\n"
    };

    auto digest = cppx::checksum::find_sha256_for_filename(manifest, "tool.tar.gz");
    tc.check(digest.has_value(), "exact filename match found");
    if (digest) {
        tc.check(*digest ==
                     "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                 "substring filename not matched");
    }
}

int main() {
    test_normalize_sha256();
    test_find_sha256_for_filename_supports_common_manifest_forms();
    test_find_sha256_for_filename_requires_exact_match();
    return tc.summary("cppx.checksum");
}
