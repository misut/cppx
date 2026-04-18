// Filesystem-facing value types and error enums. Side effects stay in
// cppx.fs.system so higher-level code can keep policy separate from I/O.

export module cppx.fs;
import std;

export namespace cppx::fs {

enum class fs_error {
    not_found,
    read_failed,
    write_failed,
    create_directories_failed,
};

inline constexpr auto to_string(fs_error error) -> std::string_view {
    switch (error) {
    case fs_error::not_found:
        return "not_found";
    case fs_error::read_failed:
        return "read_failed";
    case fs_error::write_failed:
        return "write_failed";
    case fs_error::create_directories_failed:
        return "create_directories_failed";
    }
    return "write_failed";
}

struct TextWrite {
    std::filesystem::path path;
    std::string content;
    bool skip_if_unchanged = true;
};

} // namespace cppx::fs
