// Module-consumer regression coverage for Windows boundary helpers. The
// call sites live in this interface unit so MSVC sees the same shape that
// previously ICE'd in downstream consumers such as phenotype.

export module cppx.boundary.windows.test;
import std;
import cppx.os;
import cppx.os.system;
import cppx.resource;
import cppx.unicode;

export namespace cppx::boundary::windows::test {

auto roundtrip_utf8_via_wide(
        std::string_view value) -> std::expected<std::string, cppx::unicode::unicode_error>;

auto classify_target(std::string_view value) -> cppx::resource::resource_kind;

auto resolve_target(
        std::filesystem::path const& base,
        std::string_view value) -> std::filesystem::path;

auto open_url_probe(
        std::string_view value) -> std::expected<void, cppx::os::open_error>;

} // namespace cppx::boundary::windows::test

module :private;

namespace cppx::boundary::windows::test {

auto roundtrip_utf8_via_wide(
        std::string_view value) -> std::expected<std::string, cppx::unicode::unicode_error> {
    auto wide = cppx::unicode::utf8_to_wide(value);
    if (!wide)
        return std::unexpected{wide.error()};
    return cppx::unicode::wide_to_utf8(*wide);
}

auto classify_target(std::string_view value) -> cppx::resource::resource_kind {
    return cppx::resource::classify(value);
}

auto resolve_target(
        std::filesystem::path const& base,
        std::string_view value) -> std::filesystem::path {
    return cppx::resource::resolve_path(base, value);
}

auto open_url_probe(
        std::string_view value) -> std::expected<void, cppx::os::open_error> {
    return cppx::os::system::open_url(value);
}

} // namespace cppx::boundary::windows::test
