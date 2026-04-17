import cppx.archive;
import cppx.test;
import std;

cppx::test::context tc;

void test_archive_format_roundtrip() {
    tc.check(cppx::archive::to_string(cppx::archive::ArchiveFormat::TarGz) == "tar.gz",
             "tar.gz string literal");
    tc.check(cppx::archive::to_string(cppx::archive::ArchiveFormat::TarXz) == "tar.xz",
             "tar.xz string literal");
    tc.check(cppx::archive::to_string(cppx::archive::ArchiveFormat::Zip) == "zip",
             "zip string literal");

    tc.check(cppx::archive::archive_format_from_string("tar.gz") ==
                 cppx::archive::ArchiveFormat::TarGz,
             "parse tar.gz");
    tc.check(cppx::archive::archive_format_from_string("tar.xz") ==
                 cppx::archive::ArchiveFormat::TarXz,
             "parse tar.xz");
    tc.check(cppx::archive::archive_format_from_string("zip") ==
                 cppx::archive::ArchiveFormat::Zip,
             "parse zip");
    tc.check(!cppx::archive::archive_format_from_string("tar"),
             "reject unknown format");
}

void test_extract_spec_shape() {
    auto spec = cppx::archive::ExtractSpec{
        .archive_path = "/tmp/sample.tar.gz",
        .destination_dir = "/tmp/out",
        .format = cppx::archive::ArchiveFormat::TarGz,
        .strip_components = 2,
    };

    tc.check(spec.archive_path == std::filesystem::path{"/tmp/sample.tar.gz"},
             "archive path stored");
    tc.check(spec.destination_dir == std::filesystem::path{"/tmp/out"},
             "destination dir stored");
    tc.check(spec.format == cppx::archive::ArchiveFormat::TarGz,
             "archive format stored");
    tc.check(spec.strip_components == 2, "strip components stored");
}

void test_archive_error_shape() {
    auto error = cppx::archive::archive_error{
        .code = cppx::archive::archive_error_code::tool_not_found,
        .message = "tar missing",
    };

    tc.check(error.code == cppx::archive::archive_error_code::tool_not_found,
             "archive error code stored");
    tc.check(error.message == "tar missing", "archive error message stored");
}

int main() {
    test_archive_format_roundtrip();
    test_extract_spec_shape();
    test_archive_error_shape();
    return tc.summary("cppx.archive");
}
