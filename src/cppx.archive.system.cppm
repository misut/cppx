export module cppx.archive.system;
import std;
import cppx.archive;
import cppx.env.system;
import cppx.process;
import cppx.process.system;

namespace cppx::archive::detail {

inline auto make_error(cppx::archive::archive_error_code code, std::string message)
    -> std::unexpected<cppx::archive::archive_error> {
    return std::unexpected(cppx::archive::archive_error{
        .code = code,
        .message = std::move(message),
    });
}

inline auto ensure_directory(std::filesystem::path const& path)
    -> std::expected<void, cppx::archive::archive_error> {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        return make_error(
            cppx::archive::archive_error_code::create_directories_failed,
            std::format("could not create directory '{}': {}", path.string(), ec.message()));
    }
    return {};
}

inline auto preferred_tar_program()
    -> std::expected<std::filesystem::path, cppx::archive::archive_error> {
#if defined(_WIN32)
    if (auto system_root = cppx::env::system::get("SystemRoot")) {
        auto candidate = std::filesystem::path{*system_root} / "System32" / "tar.exe";
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec))
            return candidate;
    }
#endif

    auto tar = cppx::env::system::find_in_path("tar");
    if (!tar) {
        return make_error(
            cppx::archive::archive_error_code::tool_not_found,
            "tar is required but was not found");
    }
    return *tar;
}

#if !defined(_WIN32)
inline auto unzip_program()
    -> std::expected<std::filesystem::path, cppx::archive::archive_error> {
    auto unzip = cppx::env::system::find_in_path("unzip");
    if (!unzip) {
        return make_error(
            cppx::archive::archive_error_code::tool_not_found,
            "unzip is required but was not found");
    }
    return *unzip;
}
#endif

inline auto run_command(std::filesystem::path const& program,
                        std::vector<std::string> args)
    -> std::expected<void, cppx::archive::archive_error> {
    auto result = cppx::process::system::run({
        .program = program.string(),
        .args = std::move(args),
    });
    if (!result) {
        return make_error(
            cppx::archive::archive_error_code::extract_failed,
            std::format(
                "{} failed: {}",
                program.filename().string(),
                cppx::process::to_string(result.error())));
    }
    if (result->timed_out || result->exit_code != 0) {
        return make_error(
            cppx::archive::archive_error_code::extract_failed,
            std::format(
                "{} failed with exit code {}",
                program.filename().string(),
                result->exit_code));
    }
    return {};
}

inline auto strip_relative(std::filesystem::path const& path, int strip_components)
    -> std::optional<std::filesystem::path> {
    auto stripped = std::filesystem::path{};
    auto skipped = 0;
    for (auto const& part : path) {
        if (part.empty() || part == ".")
            continue;
        if (skipped < strip_components) {
            ++skipped;
            continue;
        }
        stripped /= part;
    }
    if (stripped.empty())
        return std::nullopt;
    return stripped;
}

#if !defined(_WIN32)
inline auto move_stripped_tree(std::filesystem::path const& root,
                               std::filesystem::path const& destination,
                               int strip_components)
    -> std::expected<void, cppx::archive::archive_error> {
    for (auto const& entry : std::filesystem::recursive_directory_iterator{root}) {
        auto relative = std::filesystem::relative(entry.path(), root);
        auto stripped = strip_relative(relative, strip_components);
        if (!stripped)
            continue;

        auto target = destination / *stripped;
        auto parent = target.parent_path();
        if (!parent.empty()) {
            auto created = ensure_directory(parent);
            if (!created)
                return created;
        }

        std::error_code ec;
        if (entry.is_directory(ec)) {
            auto created = ensure_directory(target);
            if (!created)
                return created;
            continue;
        }

        if (entry.is_symlink(ec)) {
            auto link_target = std::filesystem::read_symlink(entry.path(), ec);
            if (ec) {
                return make_error(
                    cppx::archive::archive_error_code::move_failed,
                    std::format(
                        "could not read symlink '{}': {}",
                        entry.path().string(),
                        ec.message()));
            }
            std::filesystem::create_symlink(link_target, target, ec);
            if (ec) {
                return make_error(
                    cppx::archive::archive_error_code::move_failed,
                    std::format(
                        "could not create symlink '{}' -> '{}': {}",
                        target.string(),
                        link_target.string(),
                        ec.message()));
            }
            continue;
        }

        std::filesystem::rename(entry.path(), target, ec);
        if (!ec)
            continue;

        ec.clear();
        std::filesystem::copy_file(
            entry.path(),
            target,
            std::filesystem::copy_options::overwrite_existing,
            ec);
        if (ec) {
            return make_error(
                cppx::archive::archive_error_code::move_failed,
                std::format(
                    "could not move '{}' to '{}': {}",
                    entry.path().string(),
                    target.string(),
                    ec.message()));
        }
    }
    return {};
}
#endif

inline auto extract_with_tar(cppx::archive::ExtractSpec const& spec)
    -> std::expected<void, cppx::archive::archive_error> {
    auto tar = preferred_tar_program();
    if (!tar)
        return std::unexpected(tar.error());

    auto args = std::vector<std::string>{};
    switch (spec.format) {
    case cppx::archive::ArchiveFormat::TarGz:
        args = {"xzf", spec.archive_path.string()};
        break;
    case cppx::archive::ArchiveFormat::TarXz:
        args = {"xJf", spec.archive_path.string()};
        break;
    case cppx::archive::ArchiveFormat::Zip:
        args = {"xf", spec.archive_path.string()};
        break;
    }
    if (spec.strip_components > 0)
        args.push_back(std::format("--strip-components={}", spec.strip_components));
    args.push_back("-C");
    args.push_back(spec.destination_dir.string());

    return run_command(*tar, std::move(args));
}

} // namespace cppx::archive::detail

export namespace cppx::archive::system {

inline auto extract(cppx::archive::ExtractSpec const& spec)
    -> std::expected<void, cppx::archive::archive_error> {
    if (spec.strip_components < 0) {
        return cppx::archive::detail::make_error(
            cppx::archive::archive_error_code::invalid_strip_components,
            "strip_components must be non-negative");
    }

    auto created = cppx::archive::detail::ensure_directory(spec.destination_dir);
    if (!created)
        return created;

#if defined(_WIN32)
    return cppx::archive::detail::extract_with_tar(spec);
#else
    if (spec.format != cppx::archive::ArchiveFormat::Zip)
        return cppx::archive::detail::extract_with_tar(spec);

    if (spec.strip_components == 0) {
        auto unzip = cppx::archive::detail::unzip_program();
        if (!unzip)
            return std::unexpected(unzip.error());
        return cppx::archive::detail::run_command(
            *unzip,
            {"-qo", spec.archive_path.string(), "-d", spec.destination_dir.string()});
    }

    static auto counter = std::atomic<std::uint64_t>{0};
    auto scratch = std::filesystem::temp_directory_path() /
        std::format(
            "cppx-archive-{}-{}",
            std::chrono::steady_clock::now().time_since_epoch().count(),
            counter.fetch_add(1, std::memory_order_relaxed));

    auto cleanup = [&] {
        std::error_code ec;
        std::filesystem::remove_all(scratch, ec);
    };

    auto scratch_created = cppx::archive::detail::ensure_directory(scratch);
    if (!scratch_created)
        return scratch_created;

    auto unzip = cppx::archive::detail::unzip_program();
    if (!unzip) {
        cleanup();
        return std::unexpected(unzip.error());
    }

    auto extracted = cppx::archive::detail::run_command(
        *unzip,
        {"-qo", spec.archive_path.string(), "-d", scratch.string()});
    if (!extracted) {
        cleanup();
        return extracted;
    }

    auto moved = cppx::archive::detail::move_stripped_tree(
        scratch,
        spec.destination_dir,
        spec.strip_components);
    cleanup();
    return moved;
#endif
}

} // namespace cppx::archive::system
