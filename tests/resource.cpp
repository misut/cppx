import cppx.resource;
import cppx.test;
import std;

cppx::test::context tc;

void test_classify_urls() {
    tc.check(cppx::resource::classify("http://example.com")
                 == cppx::resource::resource_kind::http_url,
             "classify http URL");
    tc.check(cppx::resource::classify("https://example.com")
                 == cppx::resource::resource_kind::https_url,
             "classify https URL");
    tc.check(cppx::resource::classify("file:///tmp/report.txt")
                 == cppx::resource::resource_kind::other_url,
             "classify file URL");
    tc.check(cppx::resource::classify("mailto:hello@example.com")
                 == cppx::resource::resource_kind::other_url,
             "classify custom scheme URL");
}

void test_classify_paths() {
    tc.check(cppx::resource::classify("assets/logo.png")
                 == cppx::resource::resource_kind::filesystem_path,
             "classify relative path");
    tc.check(cppx::resource::classify("/tmp/logo.png")
                 == cppx::resource::resource_kind::filesystem_path,
             "classify absolute path");
    tc.check(cppx::resource::classify("C:\\Users\\Alice\\logo.png")
                 == cppx::resource::resource_kind::filesystem_path,
             "classify Windows drive path");
}

void test_kind_helpers() {
    tc.check(cppx::resource::is_url(cppx::resource::resource_kind::https_url),
             "https is a URL");
    tc.check(cppx::resource::is_url("https://example.com"),
             "https string is a URL");
    tc.check(!cppx::resource::is_url(
                 cppx::resource::resource_kind::filesystem_path),
             "filesystem path is not a URL");
    tc.check(!cppx::resource::is_url("assets/logo.png"),
             "filesystem string is not a URL");
    tc.check(cppx::resource::is_remote(
                 cppx::resource::resource_kind::http_url),
             "http is remote");
    tc.check(cppx::resource::is_remote("http://example.com"),
             "http string is remote");
    tc.check(!cppx::resource::is_remote(
                 cppx::resource::resource_kind::other_url),
             "other URLs are not implicitly remote");
    tc.check(!cppx::resource::is_remote("file:///tmp/report.txt"),
             "file URL string is not implicitly remote");
}

void test_resolve_path() {
    auto resolved = cppx::resource::resolve_path(
        std::filesystem::path{"/workspace/project"},
        std::filesystem::path{"assets/../image.png"});
    tc.check(resolved == std::filesystem::path{"/workspace/project/image.png"},
             "resolve relative path against base");

    auto absolute = cppx::resource::resolve_path(
        std::filesystem::path{"/workspace/project"},
        std::filesystem::path{"/tmp/image.png"});
    tc.check(absolute == std::filesystem::path{"/tmp/image.png"},
             "preserve absolute path");

    auto from_string = cppx::resource::resolve_path(
        std::filesystem::path{"/workspace/project"},
        std::string_view{"assets/../image.png"});
    tc.check(from_string == std::filesystem::path{"/workspace/project/image.png"},
             "resolve relative string path against base");

    auto windows_drive = cppx::resource::resolve_path(
        std::filesystem::path{"/workspace/project"},
        std::string_view{"C:\\Users\\Alice\\image.png"});
    tc.check(windows_drive == std::filesystem::path{"C:\\Users\\Alice\\image.png"},
             "preserve Windows drive path without rebasing");
}

int main() {
    test_classify_urls();
    test_classify_paths();
    test_kind_helpers();
    test_resolve_path();
    return tc.summary("cppx.resource");
}
