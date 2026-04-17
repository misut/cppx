import cppx.checksum.system;
import cppx.test;
import std;

cppx::test::context tc;

void test_sha256_file_matches_known_digest() {
    auto path = std::filesystem::temp_directory_path() / std::format(
        "cppx-checksum-{}",
        std::chrono::steady_clock::now().time_since_epoch().count());

    {
        auto out = std::ofstream(path, std::ios::binary);
        out << "abc";
    }

    auto digest = cppx::checksum::system::sha256_file(path);
    tc.check(digest.has_value(), "sha256_file succeeds");
    if (digest) {
        tc.check(*digest ==
                     "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                 "sha256_file returns expected digest");
    }

    std::filesystem::remove(path);
}

int main() {
    test_sha256_file_matches_known_digest();
    return tc.summary("cppx.checksum.system");
}
