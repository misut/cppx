#include "archive_fixtures.hpp"

import cppx.archive;
import cppx.archive.system;
import cppx.test;
import std;

cppx::test::context tc;

namespace {

auto unique_temp_dir(std::string_view prefix) -> std::filesystem::path {
    auto root = std::filesystem::temp_directory_path() / std::format(
        "{}-{}",
        prefix,
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root);
    return root;
}

auto write_fixture(std::filesystem::path const& path, std::string_view encoded) -> void {
    auto bytes = cppx::archive_fixture::decode_base64(encoded);
    auto file = std::ofstream(path, std::ios::binary);
    file.write(reinterpret_cast<char const*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

auto read_text(std::filesystem::path const& path) -> std::string {
    auto file = std::ifstream(path, std::ios::binary);
    return std::string{
        std::istreambuf_iterator<char>{file},
        std::istreambuf_iterator<char>{},
    };
}

void check_extracted_tree(std::filesystem::path const& destination) {
    tc.check(std::filesystem::exists(destination / "bin" / "tool.txt"),
             "tool extracted");
    tc.check(std::filesystem::exists(destination / "share" / "info.txt"),
             "info extracted");
    tc.check(read_text(destination / "bin" / "tool.txt") == "tool\n",
             "tool contents preserved");
    tc.check(read_text(destination / "share" / "info.txt") == "info\n",
             "info contents preserved");
}

} // namespace

void test_extract_tar_gz_with_strip_components() {
    auto root = unique_temp_dir("cppx-archive-targz");
    auto archive_path = root / "sample.tar.gz";
    auto destination = root / "out";
    write_fixture(archive_path, cppx::archive_fixture::sample_tar_gz_base64);

    auto result = cppx::archive::system::extract({
        .archive_path = archive_path,
        .destination_dir = destination,
        .format = cppx::archive::ArchiveFormat::TarGz,
        .strip_components = 2,
    });

    tc.check(result.has_value(), "tar.gz extract succeeds");
    if (result)
        check_extracted_tree(destination);
    std::filesystem::remove_all(root);
}

void test_extract_tar_xz_with_strip_components() {
    auto root = unique_temp_dir("cppx-archive-tarxz");
    auto archive_path = root / "sample.tar.xz";
    auto destination = root / "out";
    write_fixture(archive_path, cppx::archive_fixture::sample_tar_xz_base64);

    auto result = cppx::archive::system::extract({
        .archive_path = archive_path,
        .destination_dir = destination,
        .format = cppx::archive::ArchiveFormat::TarXz,
        .strip_components = 2,
    });

    tc.check(result.has_value(), "tar.xz extract succeeds");
    if (result)
        check_extracted_tree(destination);
    std::filesystem::remove_all(root);
}

void test_extract_zip_with_strip_components() {
    auto root = unique_temp_dir("cppx-archive-zip");
    auto archive_path = root / "sample.zip";
    auto destination = root / "out";
    write_fixture(archive_path, cppx::archive_fixture::sample_zip_base64);

    auto result = cppx::archive::system::extract({
        .archive_path = archive_path,
        .destination_dir = destination,
        .format = cppx::archive::ArchiveFormat::Zip,
        .strip_components = 2,
    });

    tc.check(result.has_value(), "zip extract succeeds");
    if (result)
        check_extracted_tree(destination);
    std::filesystem::remove_all(root);
}

void test_extract_rejects_negative_strip_components() {
    auto root = unique_temp_dir("cppx-archive-invalid");
    auto result = cppx::archive::system::extract({
        .archive_path = root / "missing.zip",
        .destination_dir = root / "out",
        .format = cppx::archive::ArchiveFormat::Zip,
        .strip_components = -1,
    });

    tc.check(!result, "negative strip components rejected");
    if (!result) {
        tc.check(result.error().code ==
                     cppx::archive::archive_error_code::invalid_strip_components,
                 "negative strip error code preserved");
    }
    std::filesystem::remove_all(root);
}

int main() {
    test_extract_tar_gz_with_strip_components();
    test_extract_tar_xz_with_strip_components();
    test_extract_zip_with_strip_components();
    test_extract_rejects_negative_strip_components();
    return tc.summary("cppx.archive.system");
}
