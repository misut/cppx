import cppx.archive;
import cppx.archive.system;
import cppx.test;
import std;

cppx::test::context tc;

namespace {

inline constexpr auto sample_tar_gz_base64 = std::string_view{
    "H4sIADgs4mkAA+2aTW/TMBjHs0kIUc5w9oEDp+yx45fmsENBSKsYArYKwakKJRVla9OlGZRvsvuuSHwlPgAS"
    "XwEnnWiWvpEqNqJ5fpKVNE1qJ9bf/smp242jKHGMAgBKCJJtpcy2wPhsO9v3CBVUAWcSFBCg+ohyCJht1ozL"
    "SRLEuilnwVkQrTlPn9bvr/l+divkz/Z/4c7Du86+47wIeuTlKXlLbkiPOfd0Ybpc6JJ+vv67n2x1Oic3u+kV"
    "V7rcL5yyNz/+oBcN3WA8Pg/dcRx9DkfBqBc6e/vOj++Pfz36+elbBTeJrOJVMD0Kgw9hfGBuHNiYfwqF/HMq"
    "hUOmZppzm5rn3wMyTAbD8JAqJbkH1JOualIQVPjNhlDkuP2kdfL0qP3mmTsNkiR2l8X1sPW63eLPm5H62pkE"
    "F18a3Cen+qLjd+suymW88a+fQ11JU39guI5N+U/zUpj/mdL5F4bblVHz/Gf973YHo1EYm6qjvP95QjD0Pyug"
    "/9WaLP9zCTQyDpT3P+EBoP/ZYIX/eVQJ30f/23my/GepN2eB5f3P4xTQ/2yQ63+3+34wMlFHef/jHkP/swP6"
    "X63J5X9ugRWPAxvzD6qQfwlCov/ZgPlL/U9RAIn6t/vk8q9Tb8YBy/sf1yMA+p8Nbvnf5GMQh9XXsYX/ScHR"
    "/6yA/ldrlvpfxeNA+fU/yYGh/9lgxfqfzzyQFAVw58nlP0u9CQPcwv8EZeh/Nljo//RdcD9yk2l1fwbSz0Ny"
    "Xsb/JMf1P0ug/9Wahfzn3wVXNA5szH/R/xhQge9/rbDc/xiA3/Q5+t/Os5D/ymf/zfkHEMX5X48AOP/bIO1u"
    "DF99Kaz/u90kis4t53+J/1OK/m8H9P9aU8j/3P4rHAdK+z/VKor+b4UV679NTikw9P+dp5B/A7P/Nv4vpEL/"
    "t0La4Rg+BEGQ+vEbOUAH8QBEAAA="};

inline constexpr auto sample_tar_xz_base64 = std::string_view{
    "/Td6WFoAAATm1rRGAgAhARYAAAB0L+Wj4EP/AlJdABcXykb8WDA1gu0ZfIL3F+TNXjDkHHSqs/4aM8vtUj//"
    "fPP8R2gUIIU7WrFifBfU+DwP2iWpFOBnr4XxnyblPi8Qsbh4bAEohmpQElwyiVTogTwgej0QNIqsYYivQ2R/"
    "xJo3mAMRy0afhDuShZedd6o+OksYjINPBvae2BqAdNyUNvlYVdAC448ZxjjujXz/ZPmdma1hE8qcuC3yeEzl"
    "nBJHmWSuI2d5gDdu6vQRwKJZ4cgVeELBxFmNGsfeRYJOc/ftcASX+QSZ7RMquVXyrtPr5ujKTIomuZ5wLp9R"
    "5J/Q5r51pmrbg1Ms3RuNIPk/vofpmju+NF4UhlFf9I5LAad5IwZxPrSejVWpcmOJyuoubLEfE2MSyzTLSkoz"
    "1dM4py0Wbtv2cgVGgE6CysrzwlHD/NEuwVtMvUKFc1cve76udGAvS64ubjqB9iHB/GfHBBc+BtUUBdwBh2dV"
    "THCT3prUoXFzZNEQfph8hbaPph13AFK7Hk5CgrqwI3gb25yT02Wjl1J+QVbT2gRkapx9pRG7y9rZ8oEKU4mie"
    "W4owj+p1/vrDgmbCeB7zaPu0tZKYCZQ0+n1WxqR3BJHtD/btlwBWGjEGqWVXALMtybmTH7M4ZGJb8OdjXGc7"
    "tfUj4eVGww4C3jC2sM3qja9yQlGXFsADo1EpkbqTAygT7TD0TjlTiob45/7AwHeB/sWG2vhHzgp9mwKkvnt6"
    "2FEd6R+AwMY32CpaYcXR+VsrjBJE1eNbAnBkhqsP4D/QNZ7HUPll+P4sbjLg0ODuTQPNSDA9dYAAAAAALnS5"
    "t3GmbO1AAHuBICIAQCtxf8BscRn+wIAAAAABFla"};

inline constexpr auto sample_symlink_tar_gz_base64 = std::string_view{
    "H4sIACbq52kAA+2bQW/TMBiGs0kIUU4c4EoOnNPPdmw3hwmVadKKVgHbhOBUhS7bqnbJaAP0zp/gzpn/iN2tCu3alUSxJ9Xf"
    "I0Vp0qRxEr32009J0JuM+55ZAEBy7s/mQszmivkcgIbMJ5xIIgWHUPhAKOXg+WC4XTO+TfJ4rJoyjIdxds92Py6TZHTP94sn"
    "5dfcSnM8evHY2/W8btz33534n/xb9DrviZqomr6qSS///r+fbJ+eHt9+1Hv8UtPTpU12ivXP+9lVEF9fj5Lgepx9T9I47Sfe"
    "zq7X6P559vrlq581nCSyjvfx9DCJz5Jx01w/sDH/BJbyz6QUnj811J4FHM8/A/8qH1wle0Rd8haIkLUCIDJijJGowaV/1HnT"
    "Pt4/7Hw8CKZxno+DVXHda3/otPeHND84bZ2/7XQbYeSfqJ2OPt+30z8Zbzz0dXAVlfqm6WNsyr9eWBr/Caj8c9MN0zief33/"
    "g95ZnF6MBumFmWOU978QGEP/swL6n9Po/BcOaKYfKO9/nAuC/meDdf5HQvQ/F9D5Nzv6V/E/KUB6Pg2CZpqlyXQwyZM0b6qg"
    "XiR5zY1zPP83/vdlkBo8Rnn/Y0Cx/mcH9D+nWfQ/M/1Aef8LeYj1Pyus9D+IoigEydD/th6df5V6ozXACvU/wbH+Z4Ub/5tc"
    "xuPE2DEq+B/jFP3PCuh/TrPof2b6gQr1PwKA/meD1f5HSYuHnKD/bT06/7PUGzTA8v7HVJeA/meD4v4HvUF6ngX5tO4Kq74k"
    "IgzL+B8nWP+zBPqf0xT5Lyyw7n5gY/7v+J8QWP+zwxr/o5wxytH/tp4i/6ZG/835B+DL9X8hJY7/NtB3HcPnLvP6f9DLs2z0"
    "MPlf8fynIOj/dkD/d5p5/gv7r78fqOD/jKH/W2GN/0PEQUr0/61nnn9zo38l/2cC/d8K+r5j+Nyl8P/RIB0a8/+y739xie9/"
    "2QH932nu+n/9/cDG/IO84/9Uov/bgEarn/+VEQ1R/7efef7Njf5Vnv8QINT4T03+KZnjeP4RBHGXvz8+xAwATgAA"};

inline constexpr auto sample_zip_base64 = std::string_view{
    "UEsDBAoAAAAAAJGukVwAAAAAAAAAAAAAAAAFABwAcm9vdC9VVAkAAxIt4mkSLeJpdXgLAAEE9gEAAAQUAAAAUEsDBAoAAAAAAJGu"
    "kVwAAAAAAAAAAAAAAAALABwAcm9vdC9pbm5lci9VVAkAAxIt4mkSLeJpdXgLAAEE9gEAAAQUAAAAUEsDBAoAAAAAAJGukVwAAAAA"
    "AAAAAAAAAAAPABwAcm9vdC9pbm5lci9iaW4vVVQJAAMSLeJpEi3iaXV4CwABBPYBAAAEFAAAAFBLAwQKAAAAAACRrpFc7xcjwwUA"
    "AAAFAAAAFwAcAHJvb3QvaW5uZXIvYmluL3Rvb2wudHh0VVQJAAMSLeJpEi3iaXV4CwABBPYBAAAEFAAAAHRvb2wKUEsDBAoAAAAA"
    "AJGukVwAAAAAAAAAAAAAAAARABwAcm9vdC9pbm5lci9zaGFyZS9VVAkAAxIt4mkSLeJpdXgLAAEE9gEAAAQUAAAAUEsDBAoAAAAA"
    "AJGukVz1SxPHBQAAAAUAAAAZABwAcm9vdC9pbm5lci9zaGFyZS9pbmZvLnR4dFVUCQADEi3iaRIt4ml1eAsAAQT2AQAABBQAAABp"
    "bmZvClBLAQIeAwoAAAAAAJGukVwAAAAAAAAAAAAAAAAFABgAAAAAAAAAEADtQQAAAAByb290L1VUBQADEi3iaXV4CwABBPYBAAAE"
    "FAAAAFBLAQIeAwoAAAAAAJGukVwAAAAAAAAAAAAAAAALABgAAAAAAAAAEADtQT8AAAByb290L2lubmVyL1VUBQADEi3iaXV4CwAB"
    "BPYBAAAEFAAAAFBLAQIeAwoAAAAAAJGukVwAAAAAAAAAAAAAAAAPABgAAAAAAAAAEADtQYQAAAByb290L2lubmVyL2Jpbi9VVAUA"
    "AxIt4ml1eAsAAQT2AQAABBQAAABQSwECHgMKAAAAAACRrpFc7xcjwwUAAAAFAAAAFwAYAAAAAAABAAAApIHNAAAAcm9vdC9pbm5l"
    "ci9iaW4vdG9vbC50eHRVVAUAAxIt4ml1eAsAAQT2AQAABBQAAABQSwECHgMKAAAAAACRrpFcAAAAAAAAAAAAAAAAEQAYAAAAAAAA"
    "ABAA7UEjAQAAcm9vdC9pbm5lci9zaGFyZS9VVAUAAxIt4ml1eAsAAQT2AQAABBQAAABQSwECHgMKAAAAAACRrpFc9UsTxwUAAAAF"
    "AAAAGQAYAAAAAAABAAAApIFuAQAAcm9vdC9pbm5lci9zaGFyZS9pbmZvLnR4dFVUBQADEi3iaXV4CwABBPYBAAAEFAAAAFBLBQYA"
    "AAAABgAGAAQCAADGAQAAAAA="};

auto decode_base64(std::string_view encoded) -> std::vector<unsigned char> {
    auto decode_char = [](unsigned char ch) -> int {
        if (ch >= 'A' && ch <= 'Z')
            return ch - 'A';
        if (ch >= 'a' && ch <= 'z')
            return ch - 'a' + 26;
        if (ch >= '0' && ch <= '9')
            return ch - '0' + 52;
        if (ch == '+')
            return 62;
        if (ch == '/')
            return 63;
        if (ch == '=')
            return -2;
        return -1;
    };

    auto bytes = std::vector<unsigned char>{};
    auto block = std::array<int, 4>{};
    auto count = 0;

    for (auto ch : encoded) {
        if (ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t')
            continue;

        auto decoded = decode_char(static_cast<unsigned char>(ch));
        if (decoded < -1)
            block[count++] = decoded;
        else if (decoded >= 0)
            block[count++] = decoded;
        else
            continue;

        if (count != 4)
            continue;

        bytes.push_back(static_cast<unsigned char>((block[0] << 2) | (block[1] >> 4)));
        if (block[2] != -2)
            bytes.push_back(static_cast<unsigned char>(((block[1] & 0x0f) << 4) | (block[2] >> 2)));
        if (block[3] != -2)
            bytes.push_back(static_cast<unsigned char>(((block[2] & 0x03) << 6) | block[3]));
        count = 0;
    }

    return bytes;
}

auto unique_temp_dir(std::string_view prefix) -> std::filesystem::path {
    auto root = std::filesystem::temp_directory_path() / std::format(
        "{}-{}",
        prefix,
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root);
    return root;
}

auto write_fixture(std::filesystem::path const& path, std::string_view encoded) -> void {
    auto bytes = decode_base64(encoded);
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
    write_fixture(archive_path, sample_tar_gz_base64);

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
    write_fixture(archive_path, sample_tar_xz_base64);

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
    write_fixture(archive_path, sample_zip_base64);

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

void test_extract_tar_gz_preserves_symlinks() {
    auto root = unique_temp_dir("cppx-archive-symlink");
    auto archive_path = root / "sample.tar.gz";
    auto destination = root / "out";
    write_fixture(archive_path, sample_symlink_tar_gz_base64);

    auto result = cppx::archive::system::extract({
        .archive_path = archive_path,
        .destination_dir = destination,
        .format = cppx::archive::ArchiveFormat::TarGz,
        .strip_components = 1,
    });

    tc.check(result.has_value(), "tar.gz with symlinks extracts");
    if (result) {
        auto link_path = destination / "bin" / "link.txt";
        auto dangling_path = destination / "dangling";
        std::error_code ec;
        tc.check(std::filesystem::is_symlink(link_path, ec),
                 "resolvable symlink preserved as symlink");
        tc.check(std::filesystem::read_symlink(link_path, ec) == "tool.txt",
                 "resolvable symlink target preserved");
        tc.check(std::filesystem::is_symlink(dangling_path, ec),
                 "dangling symlink still created");
        tc.check(std::filesystem::read_symlink(dangling_path, ec) ==
                     std::filesystem::path("../nonexistent/target"),
                 "dangling symlink target preserved");
    }
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
    test_extract_tar_gz_preserves_symlinks();
    test_extract_rejects_negative_strip_components();
    return tc.summary("cppx.archive.system");
}
