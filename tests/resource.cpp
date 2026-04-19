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
                 == cppx::resource::resource_kind::file_url,
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
    tc.check(cppx::resource::is_url(cppx::resource::resource_kind::file_url),
             "file URL is a URL");
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
                 cppx::resource::resource_kind::file_url),
             "file URL is not remote");
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

void test_resolve_file_url() {
#if defined(_WIN32)
    auto file_url = cppx::resource::resolve_file_url(
        "file:///C:/Program%20Files/cppx/resource.txt");
    tc.check(file_url == std::filesystem::path{"C:/Program Files/cppx/resource.txt"},
             "resolve file URL with empty authority");

    auto localhost = cppx::resource::resolve_file_url(
        "file://LOCALHOST/C:/Program%20Files/cppx/resource.txt");
    tc.check(localhost == std::filesystem::path{"C:/Program Files/cppx/resource.txt"},
             "resolve file URL with localhost authority");

    auto local_form = cppx::resource::resolve_file_url(
        "file:/C:/Program%20Files/cppx/resource.txt");
    tc.check(local_form == std::filesystem::path{"C:/Program Files/cppx/resource.txt"},
             "resolve RFC 8089 local-path form on Windows");

    tc.check(!cppx::resource::resolve_file_url("file:///C:/tmp/%ZZ"),
             "reject malformed percent encoding");
#else
    auto file_url = cppx::resource::resolve_file_url(
        "file:///tmp/cppx%20resource.txt");
    tc.check(file_url == std::filesystem::path{"/tmp/cppx resource.txt"},
             "resolve file URL with empty authority");

    auto localhost = cppx::resource::resolve_file_url(
        "file://LOCALHOST/tmp/cppx%20resource.txt");
    tc.check(localhost == std::filesystem::path{"/tmp/cppx resource.txt"},
             "resolve file URL with localhost authority");

    auto local_form = cppx::resource::resolve_file_url(
        "file:/tmp/cppx%20resource.txt");
    tc.check(local_form == std::filesystem::path{"/tmp/cppx resource.txt"},
             "resolve RFC 8089 local-path form on POSIX");

    tc.check(!cppx::resource::resolve_file_url("file:///tmp/%ZZ"),
             "reject malformed percent encoding");
#endif

    tc.check(!cppx::resource::resolve_file_url("file://server/share/resource.txt"),
             "reject non-local file authority");
    tc.check(!cppx::resource::resolve_file_url("https://example.com/resource.txt"),
             "reject non-file URL");
}

int main() {
    test_classify_urls();
    test_classify_paths();
    test_kind_helpers();
    test_resolve_path();
    test_resolve_file_url();
    return tc.summary("cppx.resource");
}
