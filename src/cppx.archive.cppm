export module cppx.archive;
import std;

export namespace cppx::archive {

enum class ArchiveFormat {
    TarGz,
    TarXz,
    Zip,
};

inline constexpr auto to_string(ArchiveFormat format) -> std::string_view {
    switch (format) {
    case ArchiveFormat::TarGz:
        return "tar.gz";
    case ArchiveFormat::TarXz:
        return "tar.xz";
    case ArchiveFormat::Zip:
        return "zip";
    }
    return "zip";
}

inline auto archive_format_from_string(std::string_view value)
    -> std::optional<ArchiveFormat> {
    if (value == "tar.gz")
        return ArchiveFormat::TarGz;
    if (value == "tar.xz")
        return ArchiveFormat::TarXz;
    if (value == "zip")
        return ArchiveFormat::Zip;
    return std::nullopt;
}

struct ExtractSpec {
    std::filesystem::path archive_path;
    std::filesystem::path destination_dir;
    ArchiveFormat format = ArchiveFormat::Zip;
    int strip_components = 0;
};

enum class archive_error_code {
    invalid_strip_components,
    tool_not_found,
    create_directories_failed,
    extract_failed,
    move_failed,
    unsupported,
};

inline constexpr auto to_string(archive_error_code code) -> std::string_view {
    switch (code) {
    case archive_error_code::invalid_strip_components:
        return "invalid_strip_components";
    case archive_error_code::tool_not_found:
        return "tool_not_found";
    case archive_error_code::create_directories_failed:
        return "create_directories_failed";
    case archive_error_code::extract_failed:
        return "extract_failed";
    case archive_error_code::move_failed:
        return "move_failed";
    case archive_error_code::unsupported:
        return "unsupported";
    }
    return "extract_failed";
}

struct archive_error {
    archive_error_code code = archive_error_code::extract_failed;
    std::string message;
};

} // namespace cppx::archive
