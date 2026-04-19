// System-backed filesystem helpers for cppx.fs. This is the impure
// edge where reads, directory creation, and writes touch the host FS.

export module cppx.fs.system;
import cppx.bytes;
import std;
import cppx.fs;

export namespace cppx::fs::system {

namespace detail {

inline auto ensure_parent_directory(std::filesystem::path const& path)
    -> std::expected<void, cppx::fs::fs_error> {
    auto parent = path.parent_path();
    if (parent.empty())
        return {};

    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec)
        return std::unexpected{cppx::fs::fs_error::create_directories_failed};
    return {};
}

} // namespace detail

inline auto read_text(std::filesystem::path const& path)
    -> std::expected<std::string, cppx::fs::fs_error> {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return std::unexpected{cppx::fs::fs_error::not_found};
    if (ec)
        return std::unexpected{cppx::fs::fs_error::read_failed};

    auto file = std::ifstream(path, std::ios::binary);
    if (!file)
        return std::unexpected{cppx::fs::fs_error::read_failed};

    return std::string{
        std::istreambuf_iterator<char>{file},
        std::istreambuf_iterator<char>{},
    };
}

inline auto read_bytes(std::filesystem::path const& path)
    -> std::expected<cppx::bytes::byte_buffer, cppx::fs::fs_error> {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return std::unexpected{cppx::fs::fs_error::not_found};
    if (ec)
        return std::unexpected{cppx::fs::fs_error::read_failed};

    auto file = std::ifstream(path, std::ios::binary);
    if (!file)
        return std::unexpected{cppx::fs::fs_error::read_failed};

    auto raw = std::vector<char>{
        std::istreambuf_iterator<char>{file},
        std::istreambuf_iterator<char>{},
    };
    return cppx::bytes::byte_buffer{
        cppx::bytes::bytes_view{
            std::as_bytes(std::span{raw.data(), raw.size()})}};
}

inline auto write_bytes(std::filesystem::path const& path,
                        cppx::bytes::bytes_view bytes)
    -> std::expected<void, cppx::fs::fs_error> {
    auto parent = detail::ensure_parent_directory(path);
    if (!parent)
        return std::unexpected{parent.error()};

    auto file = std::ofstream(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return std::unexpected{cppx::fs::fs_error::write_failed};

    if (!bytes.empty())
        file.write(reinterpret_cast<char const*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    if (!file)
        return std::unexpected{cppx::fs::fs_error::write_failed};
    return {};
}

inline auto append_bytes(std::filesystem::path const& path,
                         cppx::bytes::bytes_view bytes)
    -> std::expected<void, cppx::fs::fs_error> {
    auto parent = detail::ensure_parent_directory(path);
    if (!parent)
        return std::unexpected{parent.error()};

    auto file = std::ofstream(path, std::ios::binary | std::ios::app);
    if (!file)
        return std::unexpected{cppx::fs::fs_error::write_failed};

    if (!bytes.empty())
        file.write(reinterpret_cast<char const*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    if (!file)
        return std::unexpected{cppx::fs::fs_error::write_failed};
    return {};
}

inline auto write_if_changed(cppx::fs::TextWrite const& write)
    -> std::expected<bool, cppx::fs::fs_error> {
    auto parent = detail::ensure_parent_directory(write.path);
    if (!parent)
        return std::unexpected{parent.error()};

    if (write.skip_if_unchanged) {
        auto existing = read_text(write.path);
        if (existing && *existing == write.content)
            return false;
        if (!existing && existing.error() != cppx::fs::fs_error::not_found)
            return std::unexpected{existing.error()};
    }

    auto file = std::ofstream(write.path, std::ios::binary);
    if (!file)
        return std::unexpected{cppx::fs::fs_error::write_failed};
    file << write.content;
    if (!file)
        return std::unexpected{cppx::fs::fs_error::write_failed};
    return true;
}

inline auto apply_writes(std::vector<cppx::fs::TextWrite> const& writes)
    -> std::expected<bool, cppx::fs::fs_error> {
    bool changed = false;
    for (auto const& write : writes) {
        auto result = write_if_changed(write);
        if (!result)
            return std::unexpected{result.error()};
        changed = changed || *result;
    }
    return changed;
}

} // namespace cppx::fs::system
